#include <jni.h>
#include <android/native_window_jni.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>
#include "wayland_transport.h"

namespace {
std::mutex lifecycle, statusLock, socketLock;
std::thread reader;
std::atomic<bool> stopping{false};
std::atomic<unsigned> frames{0};
int listener = -1, peer = -1;
ANativeWindow* window = nullptr;
std::string socketPath, error;
unsigned lastWidth = 0, lastHeight = 0;
void setError(const char* message) { std::lock_guard<std::mutex> guard(statusLock); error = message; }
bool receiveFrame(int connection) {
    prwl_frame header{};
    iovec io{&header, sizeof(header)};
    union { cmsghdr align; char bytes[CMSG_SPACE(sizeof(int) * 8)]; } control{};
    msghdr msg{}; msg.msg_iov = &io; msg.msg_iovlen = 1;
    msg.msg_control = control.bytes; msg.msg_controllen = sizeof(control.bytes);
    ssize_t n;
    do { n = recvmsg(connection, &msg, MSG_WAITALL | MSG_CMSG_CLOEXEC); } while (n < 0 && errno == EINTR && !stopping);
    int fd = -1, count = 0;
    for (cmsghdr* cms = CMSG_FIRSTHDR(&msg); cms; cms = CMSG_NXTHDR(&msg, cms)) {
        if (cms->cmsg_level != SOL_SOCKET || cms->cmsg_type != SCM_RIGHTS || cms->cmsg_len < CMSG_LEN(0)) continue;
        size_t size = (cms->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        for (size_t i = 0; i < size; ++i) {
            int received; memcpy(&received, CMSG_DATA(cms) + i * sizeof(int), sizeof(received));
            if (count++ == 0) fd = received; else close(received);
        }
    }
    if (n == 0) { if (fd >= 0) close(fd); return false; }
    struct stat stat{};
    const int seals = fd >= 0 ? fcntl(fd, F_GET_SEALS) : -1;
    const bool valid = n == sizeof(header) && !(msg.msg_flags & MSG_CTRUNC) && count == 1 &&
        header.magic == PRWL_MAGIC && header.version == 1 && header.width && header.height &&
        header.width <= PRWL_MAX_DIM && header.height <= PRWL_MAX_DIM && header.stride >= header.width * 4 && header.stride % 4 == 0 &&
        static_cast<uint64_t>(header.stride) * header.height == header.bytes && header.bytes <= PRWL_MAX_BYTES &&
        header.format <= 1 && fstat(fd, &stat) == 0 && stat.st_size >= header.bytes &&
        seals >= 0 && (seals & (F_SEAL_SHRINK | F_SEAL_WRITE)) == (F_SEAL_SHRINK | F_SEAL_WRITE);
    if (!valid) { if (fd >= 0) close(fd); setError("Invalid SHM frame or unsealed snapshot"); return false; }
    const auto* source = static_cast<const uint8_t*>(mmap(nullptr, header.bytes, PROT_READ, MAP_SHARED, fd, 0));
    close(fd);
    if (source == MAP_FAILED) { setError("SHM snapshot mmap failed"); return false; }
    ANativeWindow_Buffer buffer{};
    int result = ANativeWindow_lock(window, &buffer, nullptr);
    if (!result) {
        if (buffer.format != WINDOW_FORMAT_RGBA_8888 && buffer.format != WINDOW_FORMAT_RGBX_8888) result = -1;
        else {
            auto* target = static_cast<uint32_t*>(buffer.bits);
            for (int y = 0; y < buffer.height; ++y) {
                unsigned sy = static_cast<uint64_t>(y) * header.height / buffer.height;
                const auto* row = reinterpret_cast<const uint32_t*>(source + sy * header.stride);
                for (int x = 0; x < buffer.width; ++x) {
                    unsigned sx = static_cast<uint64_t>(x) * header.width / buffer.width;
                    uint32_t argb = row[sx];
                    // Wayland little-endian XRGB/ARGB -> Android little-endian RGBA.
                    target[static_cast<size_t>(y) * buffer.stride + x] = 0xff000000u | (argb & 0x0000ff00u) |
                        ((argb & 0x00ff0000u) >> 16) | ((argb & 0x000000ffu) << 16);
                }
            }
        }
        int posted = ANativeWindow_unlockAndPost(window);
        if (posted) result = posted;
    }
    munmap(const_cast<uint8_t*>(source), header.bytes);
    if (result) { setError("Android Surface post failed"); return false; }
    { std::lock_guard<std::mutex> guard(statusLock); lastWidth = header.width; lastHeight = header.height; }
    ++frames;
    do { n = send(connection, &header.sequence, sizeof(header.sequence), MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
    return n == sizeof(header.sequence);
}
void run() {
    int connection;
    do { connection = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC); } while (connection < 0 && errno == EINTR && !stopping);
    if (connection < 0) { if (!stopping) setError("Presenter accept failed"); return; }
    { std::lock_guard<std::mutex> guard(socketLock); if (stopping) { close(connection); return; } peer = connection; }
    ucred credentials{}; socklen_t size = sizeof(credentials);
    if (getsockopt(connection, SOL_SOCKET, SO_PEERCRED, &credentials, &size) || credentials.uid != getuid()) setError("Presenter peer UID mismatch");
    else while (!stopping && receiveFrame(connection)) {}
    { std::lock_guard<std::mutex> guard(socketLock); close(connection); peer = -1; }
}
void stop() {
    stopping = true;
    { std::lock_guard<std::mutex> guard(socketLock);
        if (peer >= 0) shutdown(peer, SHUT_RDWR);
        if (listener >= 0) shutdown(listener, SHUT_RDWR);
    }
    if (reader.joinable()) reader.join();
    if (listener >= 0) { close(listener); listener = -1; }
    if (!socketPath.empty()) { unlink(socketPath.c_str()); socketPath.clear(); }
    if (window) { ANativeWindow_release(window); window = nullptr; }
}
}
extern "C" JNIEXPORT void JNICALL
Java_dev_proroot_app_NativeWayland_start(JNIEnv* env, jobject, jobject surface, jstring pathArg) {
    std::lock_guard<std::mutex> guard(lifecycle); stop();
    const char* path = env->GetStringUTFChars(pathArg, nullptr);
    socketPath = path; env->ReleaseStringUTFChars(pathArg, path);
    sockaddr_un address{}; address.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(address.sun_path)) {
        env->ThrowNew(env->FindClass("java/io/IOException"), "Presenter socket path too long"); socketPath.clear(); return;
    }
    strcpy(address.sun_path, socketPath.c_str()); unlink(socketPath.c_str());
    window = ANativeWindow_fromSurface(env, surface);
    if (!window) { stop(); env->ThrowNew(env->FindClass("java/io/IOException"), "Surface unavailable"); return; }
    ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || chmod(socketPath.c_str(), 0600) || listen(listener, 1)) {
        stop(); env->ThrowNew(env->FindClass("java/io/IOException"), "Presenter socket setup failed"); return;
    }
    frames = 0; stopping = false;
    { std::lock_guard<std::mutex> status(statusLock); error.clear(); lastWidth = 0; lastHeight = 0; }
    reader = std::thread(run);
}
extern "C" JNIEXPORT void JNICALL
Java_dev_proroot_app_NativeWayland_stop(JNIEnv*, jobject) { std::lock_guard<std::mutex> guard(lifecycle); stop(); }
extern "C" JNIEXPORT jstring JNICALL
Java_dev_proroot_app_NativeWayland_status(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> guard(statusLock);
    std::ostringstream json;
    // Error strings are fixed internal literals, never client-controlled.
    json << "{\"transport\":\"WL_SHM_CPU_COPY\",\"frames_presented\":" << frames.load()
         << ",\"width\":" << lastWidth << ",\"height\":" << lastHeight
         << ",\"error\":\"" << error << "\",\"end_to_end_zero_copy\":\"NOT_VERIFIED\"}";
    return env->NewStringUTF(json.str().c_str());
}
