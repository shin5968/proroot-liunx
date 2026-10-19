package dev.proroot.app

import android.app.Application
import android.os.Handler
import android.os.Looper
import android.system.Os
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.concurrent.CancellationException
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

class RootfsInstaller(application: Application) : AndroidViewModel(application) {
    companion object {
        const val VERSION = "Ubuntu Base 24.04.5 ARM64"
        const val URL_STRING = "https://cdimage.ubuntu.com/ubuntu-base/releases/24.04/release/ubuntu-base-24.04.5-base-arm64.tar.gz"
        const val SHA256 = "a91d5a93010193712d346d761372b7c9db6dfcf093893161c64ca107f05914f2"
    }
    data class State(val busy: Boolean = false, val message: String = "Not installed", val progress: Float? = null, val installed: Boolean = false)
    private val app = application
    private val main = Handler(Looper.getMainLooper())
    private val executor = Executors.newSingleThreadExecutor()
    private val cancelled = AtomicBoolean(false)
    @Volatile private var connection: HttpURLConnection? = null
    val rootfs = File(app.filesDir, "rootfs")
    private val staging = File(app.filesDir, "rootfs.installing")
    private val archive = File(app.cacheDir, "rootfs.tar.gz.part")
    private fun installed() = File(rootfs, ".proroot-installed").isFile && File(rootfs, "bin/sh").exists()
    var state by mutableStateOf(State(installed = installed(), message = if (installed()) "Installed" else "Not installed"))
        private set
    private fun report(value: State) { main.post { state = value } }
    private fun checkCancelled() { if (cancelled.get() || Thread.currentThread().isInterrupted) throw CancellationException() }
    fun cancel() { cancelled.set(true); connection?.disconnect() }
    fun install() {
        if (state.busy || state.installed) return
        cancelled.set(false)
        state = State(busy = true, message = "Preparing download")
        executor.execute {
            try {
                require(app.filesDir.usableSpace > 1536L * 1024 * 1024) { "At least 1.5 GiB of free space is required" }
                require(!rootfs.exists()) { "An existing rootfs is present; it will not be overwritten" }
                if (staging.exists()) deleteTree(staging)
                require(staging.mkdirs()) { "Cannot create staging directory" }
                val digest = MessageDigest.getInstance("SHA-256")
                val conn = URL(URL_STRING).openConnection() as HttpURLConnection
                connection = conn
                conn.connectTimeout = 15000; conn.readTimeout = 15000
                conn.instanceFollowRedirects = false
                conn.setRequestProperty("Accept-Encoding", "identity")
                try {
                    checkCancelled()
                    require(conn.responseCode == 200) { "Download failed: HTTP ${conn.responseCode}" }
                    val total = conn.contentLengthLong
                    var downloaded = 0L
                    var lastUpdate = 0L
                    conn.inputStream.use { input -> archive.outputStream().use { output ->
                        val buffer = ByteArray(65536)
                        while (true) {
                            checkCancelled()
                            val count = input.read(buffer); if (count < 0) break
                            downloaded += count
                            require(downloaded <= 512L * 1024 * 1024) { "Download exceeds size limit" }
                            output.write(buffer, 0, count); digest.update(buffer, 0, count)
                            val now = System.nanoTime()
                            if (now - lastUpdate > 200000000) {
                                report(State(true, "Downloading: ${downloaded / 1048576} MiB" + if (total > 0) " / ${total / 1048576} MiB" else "", if (total > 0) (downloaded.toFloat()/total).coerceIn(0f,1f) else null))
                                lastUpdate = now
                            }
                        }
                    } }
                    require(total <= 0 || downloaded == total) { "Incomplete download" }
                } finally { conn.disconnect(); connection = null }
                checkCancelled()
                report(State(true, "Verifying SHA-256"))
                val hash = digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
                require(hash == SHA256) { "SHA-256 verification failed" }
                report(State(true, "Extracting rootfs"))
                val extractor = RootfsArchive(
                    symlink = { target, file -> Os.symlink(target, file.absolutePath) },
                    hardlink = { target, file -> Os.link(target.absolutePath, file.absolutePath) },
                    chmod = { file, mode -> Os.chmod(file.absolutePath, mode) },
                )
                archive.inputStream().use { extractor.extract(it, staging, ::checkCancelled) }
                checkCancelled()
                require(File(staging, "bin/sh").exists()) { "Rootfs shell is missing" }
                require(File(staging, "root").mkdirs() || File(staging, "root").isDirectory)
                File(staging, "etc/resolv.conf").apply {
                    // Replace a guest resolver symlink before writing on the Android host.
                    if (java.nio.file.Files.isSymbolicLink(toPath())) require(delete())
                    writeText("nameserver 1.1.1.1\nnameserver 8.8.8.8\n")
                }
                File(staging, ".proroot-installed").writeText("$VERSION\n$SHA256\n")
                checkCancelled()
                require(staging.renameTo(rootfs)) { "Cannot finalize rootfs installation" }
                report(State(message = "Installed — ready to launch", installed = true))
            } catch (error: Exception) {
                report(State(message = if (cancelled.get() || error is CancellationException) "Installation cancelled" else "Installation failed: ${error.message}"))
            } finally {
                archive.delete()
                deleteTree(staging)
            }
        }
    }
    private fun deleteTree(directory: File) {
        if (!java.nio.file.Files.exists(directory.toPath(), java.nio.file.LinkOption.NOFOLLOW_LINKS)) return
        java.nio.file.Files.walkFileTree(directory.toPath(), object : java.nio.file.SimpleFileVisitor<java.nio.file.Path>() {
            override fun visitFile(path: java.nio.file.Path, attributes: java.nio.file.attribute.BasicFileAttributes): java.nio.file.FileVisitResult {
                java.nio.file.Files.delete(path); return java.nio.file.FileVisitResult.CONTINUE
            }
            override fun postVisitDirectory(path: java.nio.file.Path, error: java.io.IOException?): java.nio.file.FileVisitResult {
                if (error != null) throw error
                java.nio.file.Files.delete(path); return java.nio.file.FileVisitResult.CONTINUE
            }
        })
    }
    override fun onCleared() { cancel(); executor.shutdownNow(); super.onCleared() }
}
