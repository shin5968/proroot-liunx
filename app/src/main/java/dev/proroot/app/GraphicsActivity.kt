package dev.proroot.app

import android.content.ClipData
import android.content.ClipboardManager
import android.os.Bundle
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

object NativeGraphics {
    init { System.loadLibrary("proroot_host") }
    external fun run(surface: Surface): String
    external fun release()
}

class GraphicsActivity : ComponentActivity() {
    private val worker = Executors.newSingleThreadExecutor()
    private var holder: SurfaceHolder? = null
    private var busy by mutableStateOf(false)
    private var result by mutableStateOf("Surface 준비 중")
    private fun probe(surface: Surface) {
        if (busy || worker.isShutdown) return
        busy = true
        result = "Vulkan → AHB → Android Surface 검증 중"
        worker.execute {
            val text = try { JSONObject(NativeGraphics.run(surface)).toString(2) }
                catch (error: Exception) { JSONObject().put("error", error.toString()).put("end_to_end_zero_copy", "NOT_VERIFIED").toString(2) }
            File(filesDir, "graphics-host-report.json").writeText(text)
            runOnUiThread { if (!isDestroyed) { result = text; busy = false } }
        }
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Column(Modifier.fillMaxSize().background(Color(0xFF101914)).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Row {
                        Text("Vulkan · AHB 검증", color = Color.White, style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                        TextButton(onClick = { finish() }) { Text("닫기") }
                    }
                    Text("Android 호스트 테스트입니다. RADV·Wayland 전체 경로의 zero-copy 성공을 뜻하지 않습니다.", color = Color(0xFFCBD5CF), style = MaterialTheme.typography.bodySmall)
                    AndroidView(factory = { context ->
                        SurfaceView(context).apply {
                            this.holder.addCallback(object : SurfaceHolder.Callback {
                                override fun surfaceCreated(h: SurfaceHolder) { this@GraphicsActivity.holder = h; probe(h.surface) }
                                override fun surfaceChanged(h: SurfaceHolder, format: Int, width: Int, height: Int) {}
                                override fun surfaceDestroyed(h: SurfaceHolder) {
                                    this@GraphicsActivity.holder = null
                                    if (!worker.isShutdown) worker.execute { NativeGraphics.release() }
                                }
                            })
                        }
                    }, modifier = Modifier.fillMaxWidth().height(220.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(enabled = !busy && holder != null, onClick = { holder?.surface?.let { probe(it) } }) { Text("다시 검증") }
                        OutlinedButton(onClick = {
                            (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("Graphics report", result))
                        }) { Text("결과 복사") }
                    }
                    Text(result, color = Color.White, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()))
                }
            }
        }
    }
    override fun onDestroy() {
        if (!worker.isShutdown) { worker.execute { NativeGraphics.release() }; worker.shutdown() }
        super.onDestroy()
    }
}

