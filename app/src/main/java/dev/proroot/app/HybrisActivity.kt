package dev.proroot.app

import android.content.ClipData
import android.content.ClipboardManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import java.io.File
import java.util.concurrent.Executors

/** libhybris driver test menu: each test runs in a fresh guest session and shows its raw output. */
class HybrisActivity : ComponentActivity() {
    private class Test(val title: String, val command: String)
    private val bin = "/usr/lib/hybris/bin"
    private val tests = listOf(
        Test("1. 환경 점검", "echo \"driver: \$(cat /etc/proroot-driver 2>/dev/null || echo auto)\"; echo \"LD_LIBRARY_PATH=\$LD_LIBRARY_PATH\"; " +
            "for p in /system/lib64/libvulkan.so /system/lib64/libEGL.so /vendor/lib64/hw /vendor/lib64/egl /apex/com.android.runtime/lib64/bionic/libc.so /linkerconfig/ld.config.txt /dev/__properties__; do " +
            "if [ -e \$p ]; then echo \"OK      \$p\"; else echo \"MISSING \$p\"; fi; done; " +
            "echo '-- vendor vulkan/egl'; ls /vendor/lib64/hw 2>&1 | grep -i vulkan; ls /vendor/lib64/egl 2>&1"),
        Test("2. Android 속성 읽기", "for n in ro.build.version.sdk ro.build.version.release ro.board.platform ro.hardware ro.hardware.vulkan ro.hardware.egl; do " +
            "echo \"\$n=\$($bin/getprop \$n 2>&1)\"; done"),
        Test("3. Bionic 라이브러리 로드", "for l in libc.so libEGL.so libGLESv2.so libvulkan.so; do $bin/test_dlopen \$l; echo \"exit=\$?\"; done"),
        Test("4. Vulkan 장치 생성", "$bin/proroot-hybris-probe; echo \"exit=\$?\""),
        Test("5. EGL 설정 조회", "HYBRIS_EGLPLATFORM=null EGL_PLATFORM=null $bin/test_egl_configs; echo \"exit=\$?\""),
    )
    private val worker = Executors.newSingleThreadExecutor()
    private val root by lazy { File(filesDir, "rootfs") }
    private var session: PtySession? = null
    private var token = 0
    private var busy by mutableStateOf(false)
    private var debug by mutableStateOf(false)
    private var binds by mutableStateOf(true)
    private val ptyLock = Any()
    private var ptyText = ""
    private var install by mutableStateOf("확인 중")
    private var selection by mutableStateOf("")
    private var result by mutableStateOf("테스트를 선택하세요. 각 테스트는 새 게스트 세션에서 실행됩니다.")

    private fun refresh() {
        busy = true
        worker.execute {
            val text = try { HybrisRuntime.ensureInstalled(this, root) } catch (error: Exception) { "설치 실패: ${error.message ?: error}" }
            val selected = HybrisRuntime.selection(root)
            val bound = !File(root, "etc/proroot-hybris-nobind").exists()
            runOnUiThread { install = text; selection = selected; binds = bound; busy = false }
        }
    }
    private fun choose(driver: String) {
        try { HybrisRuntime.select(root, driver); selection = driver; result = "드라이버 선택: $driver\n실행 중인 터미널은 정지 후 다시 시작해야 적용됩니다." }
        catch (error: Exception) { result = "선택 실패: $error" }
    }
    private fun run(selected: List<Test>) {
        if (busy) return
        busy = true
        val current = ++token
        result = "실행 중…"
        val log = File(root, "root/hybris-test.log")
        fun finish(note: String) {
            if (current != token || isDestroyed) return
            token++
            session?.close(); session = null
            // Without a log the guest shell never ran the tests; the terminal output then holds the launcher error.
            val terminal = synchronized(ptyLock) { ptyText }
            val text = (log.takeIf { it.isFile }?.readText()?.takeLast(60000)
                ?: ("(테스트 로그 없음 — 게스트 셸이 명령을 실행하지 못했습니다)\n호스트 경로 바인드: ${if (binds) "켜짐" else "꺼짐"}\n----- 세션 터미널 출력 -----\n" + terminal.ifBlank { "(없음)" })) + note
            result = text; busy = false
            try { File(filesDir, "hybris-test-report.txt").writeText(text) } catch (_: java.io.IOException) {}
        }
        try {
            File(root, "root").mkdirs(); log.delete()
            synchronized(ptyLock) { ptyText = "" }
            File(root, "etc/proroot.bashrc").writeText(assets.open("proroot.bashrc").bufferedReader().use { it.readText() })
            File(filesDir, "tmp").mkdirs()
            session = PtySession(File(applicationInfo.nativeLibraryDir, "libproroot.so").absolutePath,
                root.absolutePath, File(filesDir, "tmp").absolutePath,
                output = { bytes -> synchronized(ptyLock) { ptyText = (ptyText + bytes.toString(Charsets.UTF_8)).takeLast(8000) } },
                exited = { code -> runOnUiThread { finish("\n[세션 종료 $code]") } })
            val body = selected.joinToString("; ") { "echo; echo '===== ${it.title} ====='; ${it.command}" }
            val prefix = if (debug) "export HYBRIS_LOGGING_LEVEL=debug; " else ""
            session?.write("$prefix{ $body; } > /root/hybris-test.log 2>&1; exit\n".toByteArray(Charsets.UTF_8))
            window.decorView.postDelayed({ finish("\n[60초 제한으로 중단됨]") }, 60000)
        } catch (error: Exception) { session?.close(); session = null; result = "실행 실패: $error"; busy = false }
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        refresh()
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                val ready = HybrisRuntime.installed(root) && selection == "hybris" && !busy
                Column(Modifier.fillMaxSize().background(Color(0xFF101914)).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("libhybris 드라이버 테스트", color = Color.White, style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                        TextButton(onClick = { finish() }) { Text("닫기") }
                    }
                    Text("libhybris: $install · 선택: ${selection.ifEmpty { "자동(RADV)" }}", color = Color(0xFFCBD5CF), style = MaterialTheme.typography.bodySmall)
                    Text("실험 기능: 순정 Android에서는 Bionic TLS 충돌로 libhybris가 SIGSEGV를 냅니다. 테스트할 때만 hybris를 선택하세요.", color = Color(0xFFFFCC80), style = MaterialTheme.typography.bodySmall)
                    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        for (driver in listOf("hybris", "radv", "none")) {
                            if (selection == driver) Button(enabled = !busy, onClick = {}) { Text(driver) }
                            else OutlinedButton(enabled = !busy, onClick = { choose(driver) }) { Text(driver) }
                        }
                        OutlinedButton(enabled = !busy, onClick = { refresh() }) { Text("재설치 확인") }
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = debug, onCheckedChange = { debug = it })
                        Text("libhybris 디버그 로그 포함", color = Color.White, style = MaterialTheme.typography.bodyMedium)
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = binds, enabled = !busy, onCheckedChange = { enabled ->
                            try {
                                val marker = File(root, "etc/proroot-hybris-nobind")
                                if (enabled) marker.delete() else { marker.parentFile?.mkdirs(); marker.writeText("") }
                                binds = enabled
                            } catch (error: Exception) { result = "설정 실패: $error" }
                        })
                        Text("Android 드라이버 경로 바인드 (/system, /vendor 등)", color = Color.White, style = MaterialTheme.typography.bodyMedium)
                    }
                    Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Button(enabled = ready, onClick = { run(tests) }, modifier = Modifier.fillMaxWidth()) { Text("전체 테스트 실행") }
                        for (test in tests) OutlinedButton(enabled = ready, onClick = { run(listOf(test)) }, modifier = Modifier.fillMaxWidth()) { Text(test.title) }
                        if (!ready && !busy) Text("테스트하려면 Ubuntu와 libhybris가 설치되어 있고 드라이버가 hybris로 선택되어야 합니다.", color = Color(0xFFFFCC80), style = MaterialTheme.typography.bodySmall)
                    }
                    OutlinedButton(onClick = {
                        (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("libhybris test", result))
                    }) { Text("결과 복사") }
                    Text(result, color = Color.White, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.weight(1.4f).fillMaxWidth().verticalScroll(rememberScrollState()))
                }
            }
        }
    }
    override fun onDestroy() {
        token++; session?.close(); session = null
        worker.shutdown()
        super.onDestroy()
    }
}
