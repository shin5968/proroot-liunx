#include <jni.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <dirent.h>
#include <cstdio>
#include <vector>
#include <set>

struct ProcessParent { pid_t pid; pid_t parent; };
static std::vector<ProcessParent> processParents() {
    std::vector<ProcessParent> result;
    DIR* directory = opendir("/proc");
    if (!directory) return result;
    while (auto* entry = readdir(directory)) {
        char* end = nullptr;
        const long id = strtol(entry->d_name, &end, 10);
        if (!end || *end || id <= 0) continue;
        std::string path = std::string("/proc/") + entry->d_name + "/stat";
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        char buffer[2048]; const ssize_t count = read(fd, buffer, sizeof(buffer) - 1); close(fd);
        if (count <= 0) continue;
        buffer[count] = 0;
        const char* tail = strrchr(buffer, ')');
        char state; int parent;
        if (tail && sscanf(tail + 1, " %c %d", &state, &parent) == 2)
            result.push_back({static_cast<pid_t>(id), static_cast<pid_t>(parent)});
    }
    closedir(directory); return result;
}

static std::string stringOf(JNIEnv* env, jstring value) {
    const char* p = env->GetStringUTFChars(value, nullptr);
    std::string result(p); env->ReleaseStringUTFChars(value, p); return result;
}
static void fail(JNIEnv* env, const char* operation) {
    std::string error = std::string(operation) + ": " + strerror(errno);
    env->ThrowNew(env->FindClass("java/io/IOException"), error.c_str());
}
extern "C" JNIEXPORT jlongArray JNICALL
Java_dev_proroot_app_NativePty_start(JNIEnv* env, jobject, jstring launcherArg, jstring rootArg, jstring tmpArg) {
    auto launcher = stringOf(env, launcherArg), root = stringOf(env, rootArg), tmp = stringOf(env, tmpArg);
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0) { fail(env, "posix_openpt"); return nullptr; }
    if (grantpt(master) || unlockpt(master)) { fail(env, "grantpt/unlockpt"); close(master); return nullptr; }
    char slaveName[256];
    if (ptsname_r(master, slaveName, sizeof(slaveName))) { fail(env, "ptsname_r"); close(master); return nullptr; }
    int slave = open(slaveName, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0) { fail(env, "open slave"); close(master); return nullptr; }
    std::string temporary = "PROROOT_TMP_DIR=" + tmp;
    // Decide before fork; Android's diagnostic shell must retain its own driver.
    // /etc/proroot-driver (written by proroot-driver in the guest) holds hybris, radv or none.
    char selected[16] = {};
    if (!launcher.empty()) {
        int file = open((root + "/etc/proroot-driver").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (file >= 0) {
            ssize_t count = read(file, selected, sizeof(selected) - 1);
            close(file);
            for (ssize_t i = 0; i < count; ++i) if (selected[i] == '\n' || selected[i] == '\r') selected[i] = 0;
        }
    }
    const bool noDriver = !strcmp(selected, "none");
    const bool hybrisInstalled = !launcher.empty() &&
        access((root + "/usr/lib/hybris/lib/libhybris/linker/q.so").c_str(), R_OK) == 0;
    const bool radvInstalled = !launcher.empty() &&
        access((root + "/usr/share/vulkan/icd.d/radeon_xclipse_icd.aarch64.json").c_str(), R_OK) == 0 &&
        access((root + "/usr/lib/xclipse/libvulkan_radeon.so").c_str(), R_OK) == 0;
    // libhybris is preferred once installed; "radv" keeps the Mesa driver.
    const bool useHybris = hybrisInstalled && !noDriver && strcmp(selected, "radv") != 0;
    const bool useRadv = radvInstalled && !noDriver && !useHybris;
    // libhybris loads the Android vendor driver from its original location, so
    // those read-only host trees must be visible at the same paths in the guest.
    static const char* const hostTrees[] = {"/system", "/system_ext", "/vendor", "/odm", "/product",
        "/apex", "/linkerconfig", "/dev/__properties__"};
    std::vector<std::string> binds;
    // The test menu can turn the binds off to isolate launcher failures.
    const bool bindTrees = access((root + "/etc/proroot-hybris-nobind").c_str(), F_OK) != 0;
    if (useHybris && bindTrees) for (const char* tree : hostTrees) if (access(tree, F_OK) == 0) binds.emplace_back(tree);
    // Build argv before fork: no allocation is allowed in the child.
    char r[] = "-r", zero[] = "-0", links[] = "--link2symlink", w[] = "-w", bind[] = "-b";
    char dir[] = "/root", guestShell[] = "/bin/bash", rcOption[] = "--rcfile";
    char rcFile[] = "/etc/proroot.bashrc", interactive[] = "-i";
    std::vector<char*> args = {launcher.data(), r, root.data(), zero, links};
    for (auto& tree : binds) { args.push_back(bind); args.push_back(tree.data()); }
    for (char* item : {w, dir, guestShell, rcOption, rcFile, interactive}) args.push_back(item);
    args.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) { fail(env, "fork"); close(master); close(slave); return nullptr; }
    if (pid == 0) {
        close(master);
        if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0) _exit(126);
        for (int fd = 0; fd < 3; ++fd) if (dup2(slave, fd) < 0) _exit(126);
        if (slave > 2) close(slave);
        // Keep child-side work minimal; no JNI or JVM callbacks after fork.
        char term[] = "TERM=xterm-256color";
        char path[] = "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/system/bin";
        char lang[] = "LANG=C.UTF-8";
        char home[] = "HOME=/root";
        char driver[] = "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_xclipse_icd.aarch64.json";
        char legacyDriver[] = "VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_xclipse_icd.aarch64.json";
        char renderNode[] = "PROROOT_DRM_RENDER_NODE=/dev/dri/renderD128";
        char nativeDrm[] = "PROROOT_NATIVE_DRM=1";
        char hybrisLibraries[] = "LD_LIBRARY_PATH=/usr/lib/hybris/lib";
        char hybrisEgl[] = "HYBRIS_EGLPLATFORM=wayland";
        char hybrisVulkan[] = "HYBRIS_VULKANPLATFORM=wayland";
        char* environment[10] = {term, path, lang, home, temporary.data()};
        int count = 5;
        if (useRadv) for (char* item : {driver, legacyDriver, renderNode, nativeDrm}) environment[count++] = item;
        if (useHybris) for (char* item : {hybrisLibraries, hybrisEgl, hybrisVulkan}) environment[count++] = item;
        environment[count] = nullptr;
        if (launcher.empty()) {
            char shell[] = "/system/bin/sh";
            char* hostArgs[] = {shell, interactive, nullptr};
            execve(shell, hostArgs, environment);
        } else {
            execve(launcher.c_str(), args.data(), environment);
        }
        constexpr char message[] = "\r\nexecve failed; inspect runtime files and Android execution policy.\r\n";
        (void)!write(2, message, sizeof(message)-1); _exit(127);
    }
    close(slave);
    jlong values[] = {master, pid};
    auto result = env->NewLongArray(2); env->SetLongArrayRegion(result, 0, 2, values); return result;
}
extern "C" JNIEXPORT void JNICALL
Java_dev_proroot_app_NativePty_resize(JNIEnv*, jobject, jint fd, jint cols, jint rows) {
    winsize size{}; size.ws_col = static_cast<unsigned short>(cols); size.ws_row = static_cast<unsigned short>(rows);
    ioctl(fd, TIOCSWINSZ, &size);
}
extern "C" JNIEXPORT jint JNICALL
Java_dev_proroot_app_NativePty_waitFor(JNIEnv*, jobject, jint pid) {
    int status = 0; pid_t result;
    do { result = waitpid(pid, &status, 0); } while (result < 0 && errno == EINTR);
    if (result < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
extern "C" JNIEXPORT void JNICALL
Java_dev_proroot_app_NativePty_stop(JNIEnv*, jobject, jint pid) {
    if (pid <= 0) return;
    // Proroot's bridge children may create their own process groups. Killing only
    // the launcher's group left an orphaned Bash spinning in wait() on Android.
    // Freeze this session's actual descendants before terminating child-first.
    std::set<pid_t> owned{pid};
    auto snapshot = processParents();
    bool child = false;
    for (const auto& process : snapshot) if (process.pid == pid && process.parent == getpid()) child = true;
    if (!child) return;
    kill(pid, SIGSTOP);
    std::vector<pid_t> order{pid};
    for (int pass = 0; pass < 16; ++pass) {
        bool changed = false, added;
        do {
            added = false;
            for (const auto& process : snapshot) {
                if (owned.count(process.parent) && owned.insert(process.pid).second) {
                    kill(process.pid, SIGSTOP); order.push_back(process.pid); added = changed = true;
                }
            }
        } while (added);
        if (!changed && pass > 0) break;
        snapshot = processParents();
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) kill(*it, SIGKILL);
}

