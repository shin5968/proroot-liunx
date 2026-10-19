package dev.proroot.app

import org.apache.commons.compress.archivers.tar.TarArchiveInputStream
import java.io.File
import java.io.InputStream
import java.util.zip.GZIPInputStream

/** Extract only into a new private staging directory. Links are created after all regular files. */
class RootfsArchive(
    private val symlink: (String, File) -> Unit,
    private val hardlink: (File, File) -> Unit,
    private val chmod: (File, Int) -> Unit,
) {
    fun extract(source: InputStream, destination: File, checkCancelled: () -> Unit = {}) {
        require(destination.isDirectory && destination.listFiles()?.isEmpty() == true) { "Staging directory must be empty" }
        val base = destination.toPath().toAbsolutePath().normalize()
        fun safe(name: String): File {
            require(!name.startsWith("/") && !name.contains('\u0000')) { "Unsafe archive path" }
            val result = base.resolve(name).normalize()
            require(result.startsWith(base)) { "Archive path escapes rootfs" }
            return result.toFile()
        }
        data class Link(val file: File, val target: String, val symbolic: Boolean)
        val links = mutableListOf<Link>()
        val directories = mutableListOf<Pair<File, Int>>()
        var bytes = 0L
        var entries = 0
        TarArchiveInputStream(GZIPInputStream(source)).use { tar ->
            while (true) {
                checkCancelled()
                val entry = tar.nextEntry ?: break
                require(++entries <= 250000) { "Too many archive entries" }
                val file = safe(entry.name)
                if (file.toPath() == base) { require(entry.isDirectory); continue }
                when {
                    entry.isDirectory -> {
                        require(file.mkdirs() || file.isDirectory) { "Cannot create directory" }
                        directories += file to (entry.mode and 511)
                    }
                    entry.isSymbolicLink || entry.isLink -> {
                        require(links.size < 50000) { "Too many archive links" }
                        links += Link(file, entry.linkName, entry.isSymbolicLink)
                    }
                    entry.isFile -> {
                        require(entry.size >= 0 && entry.size <= 2L * 1024 * 1024 * 1024 - bytes) { "Rootfs exceeds extraction limit" }
                        file.parentFile!!.mkdirs()
                        require(!file.exists()) { "Duplicate archive file" }
                        file.outputStream().use { out ->
                            val buffer = ByteArray(65536)
                            var remaining = entry.size
                            while (remaining > 0) {
                                checkCancelled()
                                val count = tar.read(buffer, 0, minOf(buffer.size.toLong(), remaining).toInt())
                                require(count > 0) { "Truncated archive entry" }
                                out.write(buffer, 0, count); remaining -= count; bytes += count
                            }
                        }
                        chmod(file, entry.mode and 511)
                    }
                    // Device nodes, sockets and FIFOs are not usable as rootless archive files.
                    else -> Unit
                }
            }
        }
        // Resolve hard links before any symlink can redirect a filesystem write.
        val pending = links.filter { !it.symbolic }.toMutableList()
        while (pending.isNotEmpty()) {
            checkCancelled()
            val count = pending.size
            val iterator = pending.iterator()
            while (iterator.hasNext()) {
                val link = iterator.next()
                val target = safe(link.target)
                if (!target.isFile) continue
                link.file.parentFile!!.mkdirs()
                hardlink(target, link.file); iterator.remove()
            }
            require(pending.size < count) { "Unresolved archive hard link" }
        }
        // All parent directories must already be real directories; never follow an earlier symlink.
        for (link in links.filter { it.symbolic }) {
            checkCancelled()
            var parent = link.file.parentFile
            while (parent != null && parent.toPath() != base) {
                require(!java.nio.file.Files.isSymbolicLink(parent.toPath())) { "Symlink in link parent" }
                parent = parent.parentFile
            }
            link.file.parentFile!!.mkdirs()
            // Absolute link targets are guest paths and must be preserved (e.g. /proc/mounts).
            symlink(link.target, link.file)
        }
        directories.sortedByDescending { it.first.path.length }.forEach { (file, mode) -> chmod(file, mode) }
    }
}
