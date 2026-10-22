package dev.proroot.app

import android.annotation.SuppressLint
import android.content.ClipData
import android.content.ClipboardManager
import android.graphics.Color as AndroidColor
import android.os.Build
import android.os.Bundle
import android.util.Base64
import android.view.WindowManager
import android.view.ActionMode
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.graphics.Rect
import android.view.inputmethod.InputMethodManager
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.webkit.JavascriptInterface
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.ViewModelProvider
import org.json.JSONObject
import java.io.File

class MainActivity : ComponentActivity() {
    private lateinit var installer: RootfsInstaller
    private val preferences by lazy { getSharedPreferences("appearance", MODE_PRIVATE) }
    private var session: PtySession? = null
    private var terminal: WebView? = null
    private var keyboardAllowed = false
    private var imeVisible = false
    private var keyboardRequest = 0
    private var selectionMode: ActionMode? = null
    private var selectionCallback: SelectionCallback? = null
    private val selectionRect = Rect()
    private var selectionHasText = false
    private var ready by mutableStateOf(false)
    private var screen by mutableStateOf("home")
    private var running by mutableStateOf(false)
    private var status by mutableStateOf("세션 대기 중")
    private var fontSize by mutableIntStateOf(14)
    private var dark by mutableStateOf(false)
    private var ctrlPressed by mutableStateOf(false)
    private var altPressed by mutableStateOf(false)
    private var generation = 0
    private var cols = 80
    private var rows = 24
    private var pendingLaunch = false
    private var desiredHost = false
    private var sessionHost = false
    private val rootfs get() = File(filesDir, "rootfs")
    private val launcher get() = File(applicationInfo.nativeLibraryDir, "libproroot.so")
    private val runtimeNames = listOf("libproroot.so", "libproroot-runtime.so", "libproroot-linker.so", "libproroot-bridge.so", "libproroot-stub-loader.so")
    private fun missingRuntime() = runtimeNames.filter { !File(applicationInfo.nativeLibraryDir, it).isFile }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        ViewCompat.setOnApplyWindowInsetsListener(window.decorView) { _, insets ->
            val visible = insets.isVisible(WindowInsetsCompat.Type.ime())
            if (imeVisible && !visible) {
                keyboardAllowed = false
                keyboardRequest++
                terminal?.evaluateJavascript("window.keyboardHidden?.()", null)
            }
            imeVisible = visible
            insets
        }
        File(filesDir, "tmp").mkdirs()
        installer = ViewModelProvider(this)[RootfsInstaller::class.java]
        fontSize = preferences.getInt("fontSize", 14).coerceIn(10, 24)
        dark = preferences.getBoolean("dark", false)
        if (preferences.getBoolean("startTerminal", false) && installer.state.installed) openTerminal()
        setContent {
            val colors = if (dark) darkColorScheme(primary = Color(0xFFB4DFC9), background = Color(0xFF15211B), surface = Color(0xFF1D2C24), outlineVariant = Color(0xFF345041), onSurfaceVariant = Color(0xFFABC0B2), secondaryContainer = Color(0xFF294B39), onSecondaryContainer = Color(0xFFB4DFC9))
                else lightColorScheme(primary = Color(0xFF174B3A), background = Color(0xFFF7F9F6), surface = Color.White, onSurface = Color(0xFF26332E), outlineVariant = Color(0xFFDBE4DD), onSurfaceVariant = Color(0xFF6F7C76), secondaryContainer = Color(0xFFDEF3E7), onSecondaryContainer = Color(0xFF174B3A))
            MaterialTheme(colorScheme = colors) {
                val fullscreen = screen == "terminal"
                var menu by remember { mutableStateOf(false) }
                var keys by remember { mutableStateOf(true) }
                LaunchedEffect(fullscreen, dark) {
                    updateSystemBars(fullscreen)
                    if (!fullscreen) { finishSelectionMode(false); terminal?.evaluateJavascript("window.deactivateTerminal?.()", null) }
                }
                BackHandler(enabled = screen != "home") { pendingLaunch = false; screen = "home" }
                Box(Modifier.fillMaxSize().background(colors.background)) {
                    // This one WebView survives navigation; PTY output keeps flowing while hidden.
                    Column(if (fullscreen) Modifier.fillMaxSize().imePadding() else Modifier.size(0.dp)) {
                        AndroidView(factory = { makeTerminal() }, modifier = Modifier.weight(1f).fillMaxWidth(), update = { view ->
                            view.setBackgroundColor(AndroidColor.BLACK)
                            if (ready) view.post {
                                view.evaluateJavascript("window.configureTerminal?.($fontSize,$dark)", null)
                                if (fullscreen) view.evaluateJavascript("window.fitTerminal?.()", null)
                            }
                        })
                        if (fullscreen && keys) {
                            Column(Modifier.fillMaxWidth().background(Color(0xFF171717))) {
                                listOf(listOf("ESC", "/", "-", "HOME", "↑", "END", "PGUP"),
                                    listOf("TAB", "CTRL", "ALT", "←", "↓", "→", "PGDN")).forEach { labels ->
                                    Row(Modifier.fillMaxWidth()) {
                                        labels.forEach { label ->
                                            val active = (label == "CTRL" && ctrlPressed) || (label == "ALT" && altPressed)
                                            TextButton(
                                                onClick = { terminal?.evaluateJavascript("window.extraKey(" + JSONObject.quote(label) + ")", null) },
                                                modifier = Modifier.weight(1f).height(36.dp),
                                                contentPadding = PaddingValues(0.dp),
                                                colors = ButtonDefaults.textButtonColors(
                                                    contentColor = if (active) Color(0xFF86EFAC) else Color(0xFFEEEEEE),
                                                    containerColor = if (active) Color(0xFF344639) else Color.Transparent)
                                            ) { Text(label, maxLines = 1, style = MaterialTheme.typography.labelMedium) }
                                        }
                                    }
                                }
                            }

                        }
                    }
                    if (!fullscreen) {
                        ProrootShell(
                            screen = screen, installer = installer, running = running, dark = dark, fontSize = fontSize,
                            startTerminal = preferences.getBoolean("startTerminal", false),
                            desktop = preferences.getString("desktop", "XFCE") ?: "XFCE",
                            runtimeAvailable = missingRuntime().isEmpty(),
                            navigate = { if (it == "terminal") openTerminal() else screen = it },
                            launch = { openTerminal() },
                            setDark = { dark = it; preferences.edit().putBoolean("dark", it).apply() },
                            setFont = { fontSize = it.coerceIn(10, 24); preferences.edit().putInt("fontSize", fontSize).apply() },
                            setStartTerminal = { preferences.edit().putBoolean("startTerminal", it).apply() },
                            selectDesktop = { preferences.edit().putString("desktop", it).apply() },
                            hostShell = { openTerminal(host = true) },
                            license = { assets.open("licenses/proroot-LICENSE.txt").bufferedReader().use { it.readText() } },
                        )
                    } else {
                        Box(Modifier.align(Alignment.TopEnd).padding(top = 4.dp, end = 6.dp)) {
                            FilledTonalIconButton(onClick = { menu = true }, modifier = Modifier.size(40.dp), shape = CircleShape) {
                                Text("⋮", style = MaterialTheme.typography.titleLarge)
                            }
                            DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                                DropdownMenuItem(text = { Text(if (sessionHost) "Android 셸 · $status" else "Ubuntu · $status") }, enabled = false, onClick = {})
                                DropdownMenuItem(text = { Text("키보드 열기") }, onClick = { menu = false; showKeyboard() })
                                DropdownMenuItem(text = { Text(if (keys) "보조 키 숨기기" else "Ctrl / 방향 키 표시") }, onClick = { keys = !keys; menu = false })
                                DropdownMenuItem(text = { Text("글자 크게") }, onClick = { fontSize = (fontSize + 1).coerceAtMost(24); preferences.edit().putInt("fontSize", fontSize).apply() })
                                DropdownMenuItem(text = { Text("글자 작게") }, onClick = { fontSize = (fontSize - 1).coerceAtLeast(10); preferences.edit().putInt("fontSize", fontSize).apply() })
                                DropdownMenuItem(text = { Text(if (running) "세션 정지" else "세션 다시 실행") }, onClick = { menu = false; if (running) stopSession() else openTerminal(desiredHost) })
                                DropdownMenuItem(text = { Text("홈으로 · 세션 유지") }, onClick = { menu = false; screen = "home" })
                            }
                        }
                        if (!running && !pendingLaunch) {
                            Surface(Modifier.align(Alignment.BottomCenter).padding(20.dp).navigationBarsPadding(), shape = MaterialTheme.shapes.large, tonalElevation = 2.dp) {
                                Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                                    Text(status, modifier = Modifier.weight(1f))
                                    TextButton(onClick = { openTerminal(desiredHost) }) { Text("다시 실행") }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    private fun updateSystemBars(fullscreen: Boolean) {
        if (Build.VERSION.SDK_INT >= 28) {
            window.attributes = window.attributes.apply { layoutInDisplayCutoutMode = if (fullscreen) WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES else WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT }
        }
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.isAppearanceLightStatusBars = !fullscreen && !dark
        controller.isAppearanceLightNavigationBars = !fullscreen && !dark
        window.statusBarColor = AndroidColor.TRANSPARENT
        window.navigationBarColor = AndroidColor.TRANSPARENT
        if (fullscreen) controller.hide(WindowInsetsCompat.Type.systemBars()) else controller.show(WindowInsetsCompat.Type.systemBars())
    }

    private fun openTerminal(host: Boolean = false) {
        if (!host && (!installer.state.installed || installer.state.busy)) { screen = "install"; return }
        if (running && sessionHost != host) stopSession()
        desiredHost = host
        pendingLaunch = !running
        screen = "terminal"
        // The view's next fit reports the real fullscreen size before spawning the shell.
        terminal?.post { terminal?.evaluateJavascript("window.fitTerminal?.()", null) }
    }

    @SuppressLint("SetJavaScriptEnabled", "JavascriptInterface")
    private fun makeTerminal(): WebView = object : WebView(this) {
        // A focused WebView textarea otherwise reopens the IME on a scrolling touch.
        // Only an explicit terminal tap or the keyboard menu enables its editor.
        override fun onCheckIsTextEditor(): Boolean = keyboardAllowed && super.onCheckIsTextEditor()
        override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection? =
            if (keyboardAllowed) super.onCreateInputConnection(outAttrs) else null
    }.apply {
        settings.javaScriptEnabled = true
        settings.allowFileAccess = false
        settings.allowContentAccess = false
        settings.blockNetworkLoads = true
        addJavascriptInterface(object {
            @JavascriptInterface fun ready() = runOnUiThread {
                ready = true
                terminal?.evaluateJavascript("window.configureTerminal?.($fontSize,$dark);window.fitTerminal?.()", null)
            }
            @JavascriptInterface fun selectionMenu(left: Float, top: Float, right: Float, bottom: Float, hasText: Boolean) = runOnUiThread {
                val view = terminal ?: return@runOnUiThread
                if (screen != "terminal") return@runOnUiThread
                selectionRect.set((left * view.width).toInt(), (top * view.height).toInt(),
                    (right * view.width).toInt(), (bottom * view.height).toInt())
                selectionHasText = hasText
                if (selectionMode == null) {
                    val callback = SelectionCallback()
                    selectionCallback = callback
                    selectionMode = view.startActionMode(callback, ActionMode.TYPE_FLOATING)
                } else {
                    selectionMode?.invalidate()
                    selectionMode?.invalidateContentRect()
                }
            }
            @JavascriptInterface fun hideSelectionMenu() = runOnUiThread { finishSelectionMode(false) }
            @JavascriptInterface fun keyboard() = runOnUiThread { showKeyboard() }
            @JavascriptInterface fun cancelKeyboard() = runOnUiThread {
                keyboardRequest++
                if (!imeVisible) keyboardAllowed = false
            }
            @JavascriptInterface fun modifiersChanged(ctrl: Boolean, alt: Boolean) = runOnUiThread {
                ctrlPressed = ctrl; altPressed = alt
            }
            @JavascriptInterface fun copySelection(text: String) = runOnUiThread {
                (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("터미널", text))
            }
            @JavascriptInterface fun pasteClipboard() = runOnUiThread {
                val clipboard = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
                val text = clipboard.primaryClip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(this@MainActivity)?.toString() ?: return@runOnUiThread
                terminal?.evaluateJavascript("window.pasteText(" + JSONObject.quote(text) + ")", null)
            }
            @JavascriptInterface fun input(data: String) = runOnUiThread { session?.write(data.toByteArray(Charsets.UTF_8)) }
            @JavascriptInterface fun resize(c: Int, r: Int) = runOnUiThread {
                cols = c.coerceIn(1, 1000); rows = r.coerceIn(1, 1000)
                session?.resize(cols, rows)
            }
            @JavascriptInterface fun viewportReady(c: Int, r: Int) = runOnUiThread {
                if (screen == "terminal" && c > 2 && r > 2 && pendingLaunch && ready) {
                    cols = c; rows = r
                    pendingLaunch = false
                    startSession()
                }
            }
        }, "Host")
        webViewClient = object : WebViewClient() {
            override fun shouldOverrideUrlLoading(view: WebView?, request: WebResourceRequest?) = true
            override fun shouldInterceptRequest(view: WebView?, request: WebResourceRequest?): android.webkit.WebResourceResponse? {
                val url = request?.url ?: return null
                val name = url.path?.removePrefix("/") ?: ""
                if (url.scheme == "https" && url.host == "app.local" && name in listOf("index.html", "xterm.js", "xterm.css", "addon-fit.js")) {
                    val mime = when { name.endsWith(".js") -> "application/javascript"; name.endsWith(".css") -> "text/css"; else -> "text/html" }
                    return android.webkit.WebResourceResponse(mime, "UTF-8", assets.open("terminal/$name"))
                }
                return android.webkit.WebResourceResponse("text/plain", "UTF-8", null)
            }
        }
        terminal = this
        loadUrl("https://app.local/index.html")
    }
    private fun finishSelectionMode(clear: Boolean) {
        selectionCallback?.clearOnFinish = clear
        selectionMode?.finish()
        selectionMode = null
        selectionCallback = null
    }

    private inner class SelectionCallback : ActionMode.Callback2() {
        var clearOnFinish = true
        override fun onCreateActionMode(mode: ActionMode, menu: Menu): Boolean {
            menu.add(0, android.R.id.copy, 0, "복사").setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS)
            menu.add(0, android.R.id.paste, 1, "붙여넣기").setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS)
            menu.add(0, android.R.id.selectAll, 2, "전체 선택").setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM)
            return true
        }
        override fun onPrepareActionMode(mode: ActionMode, menu: Menu): Boolean {
            menu.findItem(android.R.id.copy).isEnabled = selectionHasText
            menu.findItem(android.R.id.paste).isEnabled =
                (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).hasPrimaryClip()
            return true
        }
        override fun onGetContentRect(mode: ActionMode, view: View, outRect: Rect) { outRect.set(selectionRect) }
        override fun onActionItemClicked(mode: ActionMode, item: MenuItem): Boolean {
            val action = when (item.itemId) {
                android.R.id.copy -> "copy"
                android.R.id.paste -> "paste"
                android.R.id.selectAll -> "all"
                else -> return false
            }
            terminal?.evaluateJavascript("window.selectionAction(" + JSONObject.quote(action) + ")", null)
            return true
        }
        override fun onDestroyActionMode(mode: ActionMode) {
            if (selectionCallback === this) { selectionMode = null; selectionCallback = null }
            if (clearOnFinish) terminal?.evaluateJavascript("window.dismissSelection?.()", null)
        }
    }

    private fun line(text: String) { terminal?.evaluateJavascript("window.statusLine?.(${JSONObject.quote(text)})", null) }
    private fun startSession() {
        if (running) return
        if (!desiredHost && missingRuntime().isNotEmpty()) { status = "Proroot 파일이 없습니다"; line(status); return }
        val token = ++generation
        try {
            if (!desiredHost) {
                File(rootfs, "etc/proroot.bashrc").writeText(assets.open("proroot.bashrc").bufferedReader().use { it.readText() })
            }
            session = PtySession(if (desiredHost) "" else launcher.absolutePath, rootfs.absolutePath, File(filesDir, "tmp").absolutePath,
                output = { bytes ->
                    val encoded = Base64.encodeToString(bytes, Base64.NO_WRAP)
                    runOnUiThread { if (generation == token) terminal?.evaluateJavascript("window.receive('$encoded')", null) }
                }, exited = { code -> runOnUiThread {
                    if (generation == token) { status = "종료 ($code)"; running = false; session?.close(); session = null; line(status) }
                } })
            session?.resize(cols, rows)
            sessionHost = desiredHost
            running = true
            status = "실행 중"
        } catch (error: Exception) { status = "실행 실패: ${error.message}"; line(status) }
    }
    private fun showKeyboard() {
        val view = terminal ?: return
        val request = ++keyboardRequest
        // DropdownMenu owns a separate window until Compose dismisses it.
        // Request the IME after that window releases focus, not inside its click event.
        view.postDelayed({
            if (screen != "terminal" || isDestroyed || request != keyboardRequest) return@postDelayed
            keyboardAllowed = true
            view.requestFocus()
            view.evaluateJavascript("terminal.focus()") {
                view.post {
                    if (request != keyboardRequest) return@post
                    val input = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
                    input.restartInput(view)
                    input.showSoftInput(view, InputMethodManager.SHOW_IMPLICIT)
                    WindowInsetsControllerCompat(window, view).show(WindowInsetsCompat.Type.ime())
                }
            }
        }, 200)
    }
    private fun stopSession() {
        pendingLaunch = false; generation++; session?.close(); session = null; running = false; status = "세션 정지됨"; line(status)
    }
    override fun onDestroy() {
        keyboardRequest++
        finishSelectionMode(false)
        generation++; session?.close(); session = null
        terminal?.removeJavascriptInterface("Host"); terminal?.destroy(); terminal = null
        super.onDestroy()
    }
}
