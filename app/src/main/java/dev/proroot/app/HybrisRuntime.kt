package dev.proroot.app

import android.content.Context
import android.system.Os
import java.io.File

/** Installs the libhybris bundle shipped in the APK into the guest rootfs. */
object HybrisRuntime {
    private const val ARCHIVE = "hybris/libhybris-glibc-arm64.tar.gz"
    private const val SUMS = "hybris/SHA256SUMS"
    private val lock = Any()

    fun bundled(context: Context): Boolean = try { context.assets.open(SUMS).close(); true } catch (_: java.io.IOException) { false }
    fun installed(root: File): Boolean = File(root, "usr/lib/hybris/lib/libhybris/linker/q.so").isFile
    fun selection(root: File): String = File(root, "etc/proroot-driver").takeIf { it.isFile }?.readText()?.trim().orEmpty()
    fun select(root: File, driver: String) { File(root, "etc").mkdirs(); File(root, "etc/proroot-driver").writeText(driver + "\n") }

    /** Remove a tree without descending through symlinks. */
    private fun deleteTree(file: File) {
        if (!java.nio.file.Files.isSymbolicLink(file.toPath()) && file.isDirectory) file.listFiles()?.forEach { deleteTree(it) }
        file.delete()
    }

    /** Returns a status line. Does nothing when the same bundle is already installed. */
    fun ensureInstalled(context: Context, root: File): String = synchronized(lock) {
        if (!File(root, "bin/bash").exists()) return "Ubuntu rootfs가 없습니다"
        if (!bundled(context)) return if (installed(root)) "설치됨 (APK 번들 없음)" else "이 APK에는 libhybris 번들이 없습니다"
        val id = context.assets.open(SUMS).bufferedReader().use { it.readText() }.trim()
        val target = File(root, "usr/lib/hybris")
        val marker = File(target, ".bundle-id")
        if (installed(root) && marker.isFile && marker.readText().trim() == id) return "설치됨"
        val lib = File(root, "usr/lib").apply { mkdirs() }
        val stage = File(lib, ".hybris-stage")
        deleteTree(stage)
        check(stage.mkdirs()) { "스테이징 디렉터리를 만들 수 없습니다" }
        try {
            // Verify the whole asset before anything is extracted.
            val digest = java.security.MessageDigest.getInstance("SHA-256")
            context.assets.open(ARCHIVE).use { input ->
                val buffer = ByteArray(65536)
                while (true) { val count = input.read(buffer); if (count < 0) break; digest.update(buffer, 0, count) }
            }
            val hash = digest.digest().joinToString("") { "%02x".format(it) }
            check(id.startsWith(hash)) { "libhybris 번들 SHA-256 불일치" }
            context.assets.open(ARCHIVE).use { input ->
                RootfsArchive(
                    symlink = { link, file -> Os.symlink(link, file.absolutePath) },
                    hardlink = { source, file -> source.copyTo(file) },
                    chmod = { file, mode -> Os.chmod(file.absolutePath, mode) },
                ).extract(input, stage)
            }
            val extracted = File(stage, "usr/lib/hybris")
            check(File(extracted, "lib/libhybris/linker/q.so").isFile) { "libhybris 번들이 올바르지 않습니다" }
            File(extracted, ".bundle-id").writeText(id + "\n")
            deleteTree(target)
            check(extracted.renameTo(target)) { "libhybris 설치 위치로 이동 실패" }
        } finally { deleteTree(stage) }
        val bin = File(root, "usr/local/bin").apply { mkdirs() }
        for (name in listOf("proroot-driver", "proroot-hybris")) {
            val file = File(bin, name)
            File(target, "share/$name").copyTo(file, overwrite = true)
            file.setExecutable(true, false)
        }
        File(root, "etc/profile.d").mkdirs()
        File(target, "share/profile.sh").copyTo(File(root, "etc/profile.d/proroot-hybris.sh"), overwrite = true)
        "설치 완료"
    }
}
