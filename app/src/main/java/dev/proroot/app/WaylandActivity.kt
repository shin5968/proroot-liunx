package dev.proroot.app

import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

object NativeWayland {
    init { System.loadLibrary("proroot_host") }
    external fun start(surface: android.view.Surface, socket: String)
    external fun stop()
    external fun status(): String
}

class WaylandActivity : ComponentActivity() {
    private val worker = Executors.newSingleThreadExecutor()
    private var holder: SurfaceHolder? = null
    private var session: PtySession? = null
    private var resumed = false
    private var started = false
    private var generation = 0
    private var status by mutableStateOf("Wayland 준비 중")
    private var detail by mutableStateOf("Linux 앱 → wl_shm → Android Surface")
    private val root by lazy { File(filesDir, "rootfs") }
    private val logLock = Any()
    private var log = ""

    private fun startIfReady() {
        val surface = holder?.surface ?: return
        if (!resumed || started || !surface.isValid || worker.isShutdown) return
        started = true
        val token = ++generation
        val frame = holder!!.surfaceFrame
        // Keep the bootstrap CPU-copy path modest while matching the Surface aspect.
        val w = minOf(frame.width().coerceAtLeast(1), 720)
        val h = (frame.height().toLong() * w / frame.width().coerceAtLeast(1)).toInt().coerceIn(1, 2048)
        status = "Wayland compositor 시작 중"
        synchronized(logLock) { log = "" }
        worker.execute {
            try {
                check(File(root, "bin/bash").exists()) { "Ubuntu rootfs를 먼저 설치하세요." }
                val libexec = File(root, "usr/local/libexec").apply { mkdirs() }
                for (name in listOf("proroot-wayland", "proroot-wayland-demo")) {
                    val target = File(libexec, name)
                    assets.open("wayland/$name").use { source -> target.outputStream().use { source.copyTo(it) } }
                    check(target.setExecutable(true, true)) { "$name 실행 권한 설정 실패" }
                }
                assets.open("wayland-session.sh").use { source -> File(libexec, "wayland-session.sh").outputStream().use { source.copyTo(it) } }
                File(root, "etc/proroot.bashrc").writeText(assets.open("proroot.bashrc").bufferedReader().use { it.readText() })
                File(root, "tmp").mkdirs()
                File(filesDir, "tmp").mkdirs()
                NativeWayland.start(surface, File(root, "tmp/proroot-present.sock").absolutePath)
                session = PtySession(File(applicationInfo.nativeLibraryDir, "libproroot.so").absolutePath,
                    root.absolutePath, File(filesDir, "tmp").absolutePath,
                    output = { bytes -> synchronized(logLock) {
                        log = (log + bytes.toString(Charsets.UTF_8)).takeLast(32000)
                        File(filesDir, "wayland-session.log").writeText(log)
                    } },
                    exited = { code -> runOnUiThread {
                        if (token == generation) { stopSession(); status = "Wayland 세션 종료 ($code)"; detail = "재시작으로 다시 실행할 수 있습니다." }
                    } })
                session?.write("exec /bin/bash /usr/local/libexec/wayland-session.sh $w $h\n".toByteArray())
                runOnUiThread { if (token == generation) poll(token) }
            } catch (error: Exception) {
                session?.close(); session = null; NativeWayland.stop()
                runOnUiThread { if (token == generation) { status = "Wayland 시작 실패"; detail = error.message ?: error.toString() } }
            }
        }
    }
    private fun poll(token: Int) {
        if (token != generation || !started || !resumed) return
        val report = NativeWayland.status()
        File(filesDir, "wayland-report.json").writeText(report)
        val data = JSONObject(report)
        val frames = data.optInt("frames_presented")
        val error = data.optString("error")
        if (error.isNotEmpty()) { status = "화면 출력 오류"; detail = error }
        else if (frames > 0) { status = "Wayland 연결됨 · ${frames}프레임"; detail = "${data.optInt("width")} × ${data.optInt("height")} · wl_shm / CPU 복사" }
        else detail = synchronized(logLock) { log.lineSequence().filter { it.isNotBlank() }.lastOrNull()?.take(180) ?: "Linux Wayland 앱 연결 대기 중" }
        window.decorView.postDelayed({ poll(token) }, 1000)
    }
    private fun stopSession() {
        generation++; started = false
        if (!worker.isShutdown) worker.execute {
            session?.close(); session = null; NativeWayland.stop()
        }
    }
    override fun onResume() { super.onResume(); resumed = true; startIfReady() }
    override fun onPause() { resumed = false; stopSession(); super.onPause() }
    override fun onDestroy() { stopSession(); worker.shutdown(); super.onDestroy() }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Column(Modifier.fillMaxSize().background(Color(0xFF10221D))) {
                    Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp)) {
                        Text("Wayland", Modifier.weight(1f).padding(vertical = 16.dp), color = Color(0xFFA8E6C3), style = MaterialTheme.typography.titleLarge)
                        TextButton(onClick = { stopSession(); startIfReady() }) { Text("재시작") }
                        TextButton(onClick = { finish() }) { Text("닫기") }
                    }
                    Text(status, Modifier.padding(horizontal = 16.dp), color = Color.White)
                    Text(detail, Modifier.padding(16.dp), color = Color(0xFFB6CDBF), style = MaterialTheme.typography.bodySmall)
                    AndroidView(factory = { context -> SurfaceView(context).apply {
                        holder.addCallback(object : SurfaceHolder.Callback {
                            override fun surfaceCreated(h: SurfaceHolder) { this@WaylandActivity.holder = h }
                            override fun surfaceChanged(h: SurfaceHolder, format: Int, width: Int, height: Int) {
                                this@WaylandActivity.holder = h
                                if (started) stopSession()
                                startIfReady()
                            }
                            override fun surfaceDestroyed(h: SurfaceHolder) { this@WaylandActivity.holder = null; stopSession() }
                        })
                    } }, modifier = Modifier.fillMaxWidth().weight(1f))
                    Text("Wayland 출력 데모 · GPU/zero-copy 검증은 다음 단계", Modifier.padding(12.dp), color = Color(0xFFB6CDBF), style = MaterialTheme.typography.labelSmall)
                }
            }
        }
    }
}
