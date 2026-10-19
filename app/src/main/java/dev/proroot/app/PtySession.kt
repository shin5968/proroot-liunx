package dev.proroot.app

import android.os.ParcelFileDescriptor
import java.util.concurrent.Executors

object NativePty {
    init { System.loadLibrary("proroot_host") }
    external fun start(launcher: String, root: String, tmp: String): LongArray
    external fun resize(fd: Int, cols: Int, rows: Int)
    external fun waitFor(pid: Int): Int
    external fun stop(pid: Int)
}

class PtySession(launcher: String, root: String, tmp: String, private val output: (ByteArray) -> Unit, private val exited: (Int) -> Unit) {
    private val process = NativePty.start(launcher, root, tmp)
    private val descriptor = ParcelFileDescriptor.adoptFd(process[0].toInt())
    private val input = ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.dup(descriptor.fileDescriptor))
    private val writer = ParcelFileDescriptor.AutoCloseOutputStream(ParcelFileDescriptor.dup(descriptor.fileDescriptor))
    private val writes = Executors.newSingleThreadExecutor()
    @Volatile private var closed = false
    @Volatile private var reaped = false
    init {
        Thread({
            try {
                val buffer = ByteArray(8192)
                while (!closed) { val count = input.read(buffer); if (count < 0) break; output(buffer.copyOf(count)) }
            } catch (_: java.io.IOException) { /* PTY returns EIO when the slave closes. */ }
        }, "pty-reader").start()
        Thread({ val status = NativePty.waitFor(process[1].toInt()); reaped = true; exited(status) }, "pty-wait").start()
    }
    fun write(bytes: ByteArray) {
        if (!closed) try { writes.execute { try { if (!closed) writer.write(bytes) } catch (_: java.io.IOException) {} } }
        catch (_: java.util.concurrent.RejectedExecutionException) {}
    }
    fun resize(cols: Int, rows: Int) { if (!closed) NativePty.resize(descriptor.fd, cols.coerceIn(1, 1000), rows.coerceIn(1, 1000)) }
    fun close() {
        if (closed) return
        closed = true
        if (!reaped) NativePty.stop(process[1].toInt())
        writes.shutdownNow()
        input.close(); writer.close(); descriptor.close()
    }
}
