package dev.proroot.app

import android.annotation.SuppressLint
import android.graphics.Color
import android.os.Bundle
import android.util.Base64
import android.view.SurfaceView
import android.webkit.JavascriptInterface
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import org.json.JSONObject
import java.io.File

class MainActivity : ComponentActivity() {
    private var session: PtySession? = null
    private var terminal: WebView? = null
    private var ready by mutableStateOf(false)
    private var generation = 0
    private var status by mutableStateOf("Stopped")
    private var running by mutableStateOf(false)
    private var cols = 80
    private var rows = 24
    private val rootfs get() = File(filesDir, "rootfs")
    private val launcher get() = File(applicationInfo.nativeLibraryDir, "libproroot.so")
    private val runtimeNames = listOf("libproroot.so", "libproroot-runtime.so", "libproroot-linker.so", "libproroot-bridge.so", "libproroot-stub-loader.so")
    private fun missingRuntime() = runtimeNames.filter { !File(applicationInfo.nativeLibraryDir, it).isFile }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        File(filesDir, "tmp").mkdirs()
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                var tab by remember { mutableIntStateOf(0) }
                Surface(Modifier.fillMaxSize()) {
                    Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding()) {
                        Text("Proroot Linux", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.padding(16.dp))
                        Text(status, modifier = Modifier.padding(horizontal = 16.dp))
                        TabRow(selectedTabIndex = tab) {
                            listOf("Terminal", "GUI", "Container").forEachIndexed { index, name ->
                                Tab(selected = tab == index, onClick = { tab = index }, text = { Text(name) })
                            }
                        }
                        Row(Modifier.padding(8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Button(enabled = !running && ready, onClick = { startSession(false) }) { Text("Host shell") }
                            Button(enabled = !running && ready, onClick = { startSession(true) }) { Text("Linux") }
                            OutlinedButton(enabled = running, onClick = { stopSession() }) { Text("Stop") }
                        }
                        if (tab == 0) {
                            Row(Modifier.padding(horizontal = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                TextButton(enabled = running, onClick = { session?.write(byteArrayOf(3)) }) { Text("Ctrl-C") }
                                TextButton(enabled = running, onClick = { session?.write(byteArrayOf(9)) }) { Text("Tab") }
                                TextButton(enabled = running, onClick = { session?.write(byteArrayOf(27)) }) { Text("Esc") }
                            }
                        }
                        Box(Modifier.weight(1f).fillMaxWidth()) {
                            // Keep the WebView mounted across tabs so terminal state and output survive.
                            AndroidView(factory = { makeTerminal() }, modifier = if (tab == 0) Modifier.fillMaxSize() else Modifier.size(0.dp))
                            if (tab == 1) {
                                Column(Modifier.fillMaxSize()) {
                                    Text("GUI surface ready for integration. Vulkan/WSI is not connected yet.", modifier = Modifier.padding(16.dp))
                                    AndroidView(factory = { SurfaceView(it).apply { setBackgroundColor(Color.BLACK) } }, modifier = Modifier.fillMaxSize())
                                }
                            }
                            if (tab == 2) {
                                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                                    Text("Default container", style = MaterialTheme.typography.titleLarge)
                                    Text("Rootfs: ${rootfs.absolutePath}")
                                    Text("Shell: ${if (File(rootfs, "bin/sh").exists()) "found" else "not installed"}")
                                    Text("Runtime: ${if (missingRuntime().isEmpty()) "installed" else "missing ${missingRuntime().joinToString()}"}")
                                    Text("ARM64 · rootless · glibc\nInstall the runtime in jniLibs before building. Provision a trusted Ubuntu ARM64 rootfs using the README instructions.")
                                    Text("Prototype uses target SDK 28 for rootfs execution compatibility. Store distribution and newer-target execution need separate validation.")
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    @SuppressLint("SetJavaScriptEnabled", "JavascriptInterface")
    private fun makeTerminal(): WebView = WebView(this).apply {
        setBackgroundColor(Color.rgb(16, 21, 29))
        settings.javaScriptEnabled = true
        settings.allowFileAccess = false
        settings.allowContentAccess = false
        settings.blockNetworkLoads = true
        addJavascriptInterface(object {
            @JavascriptInterface fun ready() = runOnUiThread {
                ready = true
                line("Ready. Host shell tests PTY; Linux requires a provisioned runtime and rootfs.")
            }
            @JavascriptInterface fun input(data: String) = runOnUiThread { session?.write(data.toByteArray(Charsets.UTF_8)) }
            @JavascriptInterface fun resize(c: Int, r: Int) = runOnUiThread {
                cols = c.coerceIn(1,1000); rows = r.coerceIn(1,1000); session?.resize(cols, rows)
            }
        }, "Host")
        webViewClient = object : WebViewClient() {
            override fun shouldOverrideUrlLoading(view: WebView?, request: WebResourceRequest?) = true
            override fun shouldInterceptRequest(view: WebView?, request: WebResourceRequest?): android.webkit.WebResourceResponse? {
                val url = request?.url ?: return null
                if (url.scheme == "https" && url.host == "app.local") {
                    val name = url.path?.removePrefix("/") ?: ""
                    if (name !in listOf("index.html", "xterm.js", "xterm.css", "addon-fit.js")) return android.webkit.WebResourceResponse("text/plain", "UTF-8", null)
                    val mime = when { name.endsWith(".js") -> "application/javascript"; name.endsWith(".css") -> "text/css"; else -> "text/html" }
                    return android.webkit.WebResourceResponse(mime, "UTF-8", assets.open("terminal/$name"))
                }
                return android.webkit.WebResourceResponse("text/plain", "UTF-8", null)
            }
        }
        terminal = this
        loadUrl("https://app.local/index.html")
    }
    private fun line(text: String) { terminal?.evaluateJavascript("window.statusLine(${JSONObject.quote(text)})", null) }
    private fun startSession(linux: Boolean) {
        if (running) return
        if (linux && (missingRuntime().isNotEmpty() || !File(rootfs, "bin/sh").exists())) {
            line("Linux unavailable. See Container tab and README for runtime/rootfs setup."); return
        }
        val token = ++generation
        try {
            val newSession = PtySession(if (linux) launcher.absolutePath else "", rootfs.absolutePath, File(filesDir,"tmp").absolutePath,
                output = { bytes ->
                    val encoded = Base64.encodeToString(bytes, Base64.NO_WRAP)
                    runOnUiThread { if (generation == token) terminal?.evaluateJavascript("window.receive('$encoded')", null) }
                }, exited = { code -> runOnUiThread {
                    if (generation == token) { status = "Exited ($code)"; running = false; session?.close(); session = null; line(status) }
                } })
            session = newSession
            newSession.resize(cols, rows)
            running = true; status = if (linux) "Linux running" else "Android host shell running"
        } catch (error: Exception) { status = "Start failed: ${error.message}"; line(status) }
    }
    private fun stopSession() {
        generation++; session?.close(); session = null; running = false; status = "Stopped"; line(status)
    }
    override fun onDestroy() {
        generation++; session?.close(); session = null
        terminal?.removeJavascriptInterface("Host"); terminal?.destroy(); terminal = null
        super.onDestroy()
    }
}


