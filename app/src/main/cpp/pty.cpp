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
    const bool radvInstalled = !launcher.empty() &&
        access((root + "/usr/share/vulkan/icd.d/radeon_xclipse_icd.aarch64.json").c_str(), R_OK) == 0 &&
        access((root + "/usr/lib/xclipse/libvulkan_radeon.so").c_str(), R_OK) == 0;
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
        char* environment[] = {term, path, lang, home, temporary.data(),
            radvInstalled ? driver : nullptr, legacyDriver, renderNode, nativeDrm, nullptr};
        if (launcher.empty()) {
            char shell[] = "/system/bin/sh"; char interactive[] = "-i";
            char* args[] = {shell, interactive, nullptr};
            execve(shell, args, environment);
        } else {
            char r[] = "-r", zero[] = "-0", links[] = "--link2symlink", w[] = "-w";
            char dir[] = "/root", shell[] = "/bin/bash", rcOption[] = "--rcfile";
            char rcFile[] = "/etc/proroot.bashrc", interactive[] = "-i";
            char* args[] = {launcher.data(), r, root.data(), zero, links, w, dir, shell, rcOption, rcFile, interactive, nullptr};
            execve(launcher.c_str(), args, environment);
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

