package dev.proroot.app

import org.apache.commons.compress.archivers.tar.TarArchiveEntry
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream
import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.nio.file.Files
import java.util.zip.GZIPOutputStream

class RootfsArchiveTest {
    private fun archive(vararg entries: Triple<String, Byte, String>): ByteArray {
        val bytes = ByteArrayOutputStream()
        TarArchiveOutputStream(GZIPOutputStream(bytes)).use { tar ->
            for ((name, type, value) in entries) {
                val entry = TarArchiveEntry(name, type, true)
                val content = value.toByteArray()
                if (type == '0'.code.toByte()) entry.size = content.size.toLong() else entry.linkName = value
                tar.putArchiveEntry(entry)
                if (type == '0'.code.toByte()) tar.write(content)
                tar.closeArchiveEntry()
            }
        }
        return bytes.toByteArray()
    }
    private fun extractor() = RootfsArchive(
        symlink = { target, file -> Files.createSymbolicLink(file.toPath(), java.nio.file.Paths.get(target)); Unit },
        hardlink = { target, file -> Files.createLink(file.toPath(), target.toPath()); Unit },
        chmod = { _, _ -> },
    )
    @Test fun extractsFilesAndDeferredLinks() {
        val dir = Files.createTempDirectory("rootfs-valid").toFile()
        val data = archive(Triple("bin", '2'.code.toByte(), "usr/bin"), Triple("usr/bin/sh", '0'.code.toByte(), "shell"), Triple("usr/bin/copy", '1'.code.toByte(), "usr/bin/sh"))
        extractor().extract(ByteArrayInputStream(data), dir)
        assertEquals("shell", dir.resolve("bin/sh").readText())
        assertTrue(Files.isSameFile(dir.resolve("usr/bin/sh").toPath(), dir.resolve("usr/bin/copy").toPath()))
    }
    @Test fun rejectsTraversalAndAbsoluteEntry() {
        for (name in listOf("../escape", "/escape", "a/../../escape")) {
            val dir = Files.createTempDirectory("rootfs-traversal").toFile()
            assertThrows(IllegalArgumentException::class.java) {
                extractor().extract(ByteArrayInputStream(archive(Triple(name, '0'.code.toByte(), "bad"))), dir)
            }
        }
    }
    @Test fun rejectsWriteThroughSymlinkParent() {
        val dir = Files.createTempDirectory("rootfs-links").toFile()
        val outside = Files.createTempDirectory("outside")
        val data = archive(Triple("redirect", '2'.code.toByte(), outside.toString()), Triple("redirect/escape", '2'.code.toByte(), "target"))
        assertThrows(IllegalArgumentException::class.java) { extractor().extract(ByteArrayInputStream(data), dir) }
        assertFalse(Files.exists(outside.resolve("escape")))
    }
    @Test fun rejectsHardlinkTraversal() {
        val dir = Files.createTempDirectory("rootfs-hardlink").toFile()
        assertThrows(IllegalArgumentException::class.java) {
            extractor().extract(ByteArrayInputStream(archive(Triple("link", '1'.code.toByte(), "../outside"))), dir)
        }
    }
    @Test fun cancellationStopsExtraction() {
        val dir = Files.createTempDirectory("rootfs-cancel").toFile()
        assertThrows(java.util.concurrent.CancellationException::class.java) {
            extractor().extract(ByteArrayInputStream(archive(Triple("file", '0'.code.toByte(), "test"))), dir) { throw java.util.concurrent.CancellationException() }
        }
    }
    @Test fun officialUbuntuArchiveSmoke() {
        val path = System.getenv("ROOTFS_TEST_ARCHIVE") ?: return
        val dir = Files.createTempDirectory("ubuntu-rootfs").toFile()
        java.io.File(path).inputStream().use { extractor().extract(it, dir) }
        assertTrue(dir.resolve("bin/sh").exists())
        assertTrue(dir.resolve("usr/bin/apt").isFile)
    }
}
