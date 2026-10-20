package dev.proroot.app

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun ProrootShell(
    screen: String, installer: RootfsInstaller, running: Boolean, dark: Boolean, fontSize: Int,
    startTerminal: Boolean, desktop: String, runtimeAvailable: Boolean,
    navigate: (String) -> Unit, launch: () -> Unit, setDark: (Boolean) -> Unit,
    setFont: (Int) -> Unit, setStartTerminal: (Boolean) -> Unit, selectDesktop: (String) -> Unit,
    hostShell: () -> Unit, license: () -> String,
) {
    val colors = MaterialTheme.colorScheme
    Column(Modifier.fillMaxSize().background(colors.background).statusBarsPadding().navigationBarsPadding()) {
        Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(18.dp)) {
            when (screen) {
                "home" -> {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        Badge("terminal", filled = true)
                        Column(Modifier.weight(1f)) {
                            Text("Proroot Linux", fontSize = 20.sp, fontWeight = FontWeight.Bold)
                            Muted("내 손안의 Linux 작업공간", 12)
                        }
                        Chip(if (installer.state.installed) "설치됨" else "설치 필요")
                    }
                    Spacer(Modifier.height(4.dp))
                    LaunchCard(if (installer.state.installed) "터미널 열기" else "Ubuntu 설치하고 시작",
                        if (running) "실행 중인 세션으로 돌아갑니다" else if (installer.state.installed) "Linux 셸로 바로 이동합니다" else "Ubuntu Base를 먼저 준비해요", "terminal", true, launch)
                    LaunchCard("데스크톱 모드", "GUI 연결을 준비하고 있어요", "desktop", false, { navigate("desktop") })
                    Spacer(Modifier.height(4.dp))
                    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) { Eyebrow("컨테이너"); Text("Linux 환경", fontSize = 19.sp, fontWeight = FontWeight.Medium) }
                    CardBox(onClick = { navigate("install") }) {
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            Badge("package")
                            Column(Modifier.weight(1f)) {
                                Text("Ubuntu 24.04 LTS", fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
                                Muted("ARM64 · glibc 2.39", 12)
                            }
                            Text("›", fontSize = 25.sp, color = colors.primary)
                        }
                        Spacer(Modifier.height(10.dp))
                        Muted(if (installer.state.busy) installer.state.message else if (installer.state.installed) "설치 완료 · 기기에서 직접 실행" else "처음 한 번 다운로드가 필요해요", 12)
                    }
                    Column(Modifier.padding(10.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text("기기 안에서 직접 실행", fontSize = 13.sp, fontWeight = FontWeight.SemiBold, color = colors.primary)
                        Muted("Proroot v1.2.8 기본 포함", 12)
                    }
                }
                "install" -> {
                    Header("Ubuntu 관리", "컨테이너")
                    CardBox {
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            Badge("package")
                            Column { Text("Ubuntu 24.04.5 LTS", fontWeight = FontWeight.Bold); Muted("ARM64 Base rootfs", 12) }
                        }
                    }
                    when {
                        installer.state.busy -> {
                            Header(when { installer.state.message.startsWith("Downloading") -> "Ubuntu 다운로드"; installer.state.message.startsWith("Verifying") -> "파일 무결성 검증"; installer.state.message.startsWith("Extracting") -> "Ubuntu 설치 중"; else -> "설치 준비 중" }, "설치 진행 중")
                            val progress = installer.state.progress
                            if (progress == null) LinearProgressIndicator(Modifier.fillMaxWidth()) else {
                                Text("${(progress * 100).toInt()}%", fontSize = 32.sp, fontWeight = FontWeight.Bold, color = colors.primary)
                                LinearProgressIndicator(progress = { progress }, modifier = Modifier.fillMaxWidth())
                            }
                            Muted(installer.state.message)
                            Text("다운로드 → SHA-256 검증 → 압축 해제", fontSize = 13.sp)
                            OutlinedButton(onClick = { installer.cancel() }, modifier = Modifier.fillMaxWidth().height(52.dp)) { Text("설치 취소") }
                        }
                        installer.state.installed -> {
                            Header("Ubuntu가 준비됐어요", "설치 완료")
                            Muted("Linux 앱을 실행할 수 있어요. 터미널에서 첫 명령어를 입력해 보세요.")
                            PrimaryButton("터미널 시작", launch, enabled = runtimeAvailable)
                        }
                        else -> {
                            Header("Ubuntu를 준비할까요?", "처음 한 번만 설치")
                            Muted("Ubuntu 공식 서버에서 다운로드해요. 설치 후에는 오프라인에서도 셸을 사용할 수 있어요.")
                            CardBox {
                                Detail("Proroot", if (runtimeAvailable) "기본 포함 · v1.2.8" else "런타임 파일 누락")
                                Detail("필요한 여유 공간", "1.5 GiB 이상")
                                Detail("다운로드", "약 29 MiB")
                            }
                            if (installer.state.message != "Not installed") Text(installer.state.message, color = colors.error, fontSize = 13.sp)
                            PrimaryButton("Ubuntu 다운로드 및 설치", { installer.install() }, enabled = runtimeAvailable && !running)
                            if (running) Muted("설치 전에 실행 중인 셸을 종료해 주세요.", 12)
                        }
                    }
                }
                "desktop" -> {
                    Header("데스크톱", "GUI 환경")
                    CardBox { Text("GUI 연결 준비 중", color = colors.primary, fontWeight = FontWeight.SemiBold); Spacer(Modifier.height(6.dp)); Muted("선호하는 환경만 저장해요. 아직 설치하거나 실행하지 않습니다.", 13) }
                    Text("어떤 데스크톱을 선호하세요?", fontSize = 19.sp, fontWeight = FontWeight.SemiBold)
                    var selected by remember(desktop) { mutableStateOf(desktop) }
                    listOf("XFCE" to "가볍고 익숙한 데스크톱", "GNOME" to "간결하고 현대적인 환경", "KDE Plasma" to "유연한 맞춤형 데스크톱").forEach { (name, description) ->
                        CardBox(selected = selected == name, onClick = { selected = name; selectDesktop(name) }) {
                            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                                Badge("desktop")
                                Column(Modifier.weight(1f)) { Text(name, fontWeight = FontWeight.Bold); Muted(description, 12) }
                                Chip("준비 중")
                            }
                        }
                    }
                    Spacer(Modifier.height(12.dp))
                    Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        UiIcon("desktop", Modifier.size(44.dp))
                        Text("Linux 데스크톱을 준비하고 있어요", fontWeight = FontWeight.SemiBold)
                        Muted("Vulkan · libhybris · 화면 출력 연결 예정", 12)
                    }
                }
                "settings" -> {
                    Header("설정", "PROROOT LINUX")
                    var autoStart by remember { mutableStateOf(startTerminal) }
                    CardBox {
                        Text("앱 설정", fontWeight = FontWeight.SemiBold)
                        Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) { Text("터미널로 바로 시작"); Muted("Ubuntu 설치 후 적용돼요", 12) }
                            Switch(checked = autoStart, onCheckedChange = { autoStart = it; setStartTerminal(it) })
                        }
                        HorizontalDivider()
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("터미널 글꼴 크기", Modifier.weight(1f))
                            TextButton(onClick = { setFont(fontSize - 1) }) { Text("−") }
                            Text("$fontSize", fontFamily = FontFamily.Monospace)
                            TextButton(onClick = { setFont(fontSize + 1) }) { Text("+") }
                        }
                        HorizontalDivider()
                        Row(Modifier.padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                            Text("어두운 테마", Modifier.weight(1f)); Switch(checked = dark, onCheckedChange = setDark)
                        }
                    }
                    CardBox {
                        Text("정보", fontWeight = FontWeight.SemiBold)
                        Detail("Proroot", "v1.2.8")
                        Detail("앱 버전", BuildConfig.VERSION_NAME)
                        var showLicense by remember { mutableStateOf(false) }
                        TextButton(onClick = { showLicense = true }) { Text("Proroot 라이선스") }
                        if (showLicense) AlertDialog(onDismissRequest = { showLicense = false }, title = { Text("Proroot · coderred") }, text = { Text(remember { license() }, modifier = Modifier.verticalScroll(rememberScrollState())) }, confirmButton = { TextButton(onClick = { showLicense = false }) { Text("닫기") } })
                    }
                    CardBox { Text("실제 Linux 터미널", fontWeight = FontWeight.SemiBold, color = colors.primary); Spacer(Modifier.height(6.dp)); Muted("이 앱의 터미널은 Proroot와 PTY에 연결됩니다. 데스크톱 기능은 아직 준비 중이에요.", 13) }
                    OutlinedButton(onClick = hostShell, modifier = Modifier.fillMaxWidth().height(52.dp)) { Text("Android 진단 셸") }
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        HorizontalDivider(color = colors.outlineVariant)
        Row(Modifier.fillMaxWidth().height(74.dp).background(colors.surface), horizontalArrangement = Arrangement.SpaceEvenly) {
            listOf("home" to "홈", "terminal" to "터미널", "desktop" to "데스크톱", "settings" to "설정").forEach { (target, label) ->
                val active = screen == target || (screen == "install" && target == "home")
                Column(Modifier.weight(1f).fillMaxHeight().clickable { navigate(target) }, horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                    UiIcon(target, Modifier.size(24.dp), if (active) colors.primary else colors.onSurfaceVariant.copy(alpha = .6f))
                    Spacer(Modifier.height(5.dp))
                    Text(label, fontSize = 11.sp, color = if (active) colors.primary else colors.onSurfaceVariant)
                }
            }
        }
    }
}

@Composable private fun Header(title: String, eyebrow: String) { Column(verticalArrangement = Arrangement.spacedBy(5.dp)) { Eyebrow(eyebrow); Text(title, fontSize = 25.sp, fontWeight = FontWeight.Bold) } }
@Composable private fun Eyebrow(text: String) { Text(text, fontSize = 11.sp, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.primary) }
@Composable private fun Muted(text: String, size: Int = 14) { Text(text, fontSize = size.sp, lineHeight = (size + 7).sp, color = MaterialTheme.colorScheme.onSurfaceVariant) }
@Composable private fun Chip(text: String) { Surface(color = MaterialTheme.colorScheme.primary.copy(alpha = .09f), shape = RoundedCornerShape(100.dp)) { Text(text, Modifier.padding(horizontal = 10.dp, vertical = 6.dp), fontSize = 10.sp, color = MaterialTheme.colorScheme.primary, maxLines = 1) } }
@Composable private fun Badge(kind: String, filled: Boolean = false) { Box(Modifier.size(48.dp).background(if (filled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.primary.copy(alpha = .1f), RoundedCornerShape(15.dp)), contentAlignment = Alignment.Center) { UiIcon(kind, Modifier.size(25.dp), if (filled) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.primary) } }
@Composable private fun Detail(label: String, value: String) { Row(Modifier.fillMaxWidth().padding(vertical = 10.dp), horizontalArrangement = Arrangement.SpaceBetween) { Muted(label, 13); Text(value, fontSize = 13.sp, fontWeight = FontWeight.Medium) } }
@Composable private fun PrimaryButton(text: String, onClick: () -> Unit, enabled: Boolean = true) { Button(onClick = onClick, enabled = enabled, modifier = Modifier.fillMaxWidth().heightIn(min = 54.dp), shape = RoundedCornerShape(18.dp)) { Text(text, fontSize = 15.sp, fontWeight = FontWeight.SemiBold) } }
@Composable private fun CardBox(selected: Boolean = false, onClick: (() -> Unit)? = null, content: @Composable ColumnScope.() -> Unit) {
    val colors = MaterialTheme.colorScheme
    Column(Modifier.fillMaxWidth().background(if (selected) colors.primary.copy(alpha = .06f) else colors.surface, RoundedCornerShape(20.dp)).border(1.dp, if (selected) colors.primary else colors.outlineVariant, RoundedCornerShape(20.dp)).then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier).padding(16.dp), content = content)
}
@Composable private fun LaunchCard(title: String, subtitle: String, icon: String, primary: Boolean, onClick: () -> Unit) {
    val colors = MaterialTheme.colorScheme
    val foreground = if (primary) colors.onPrimary else colors.onSurface
    Row(Modifier.fillMaxWidth().heightIn(min = if (primary) 116.dp else 100.dp).background(if (primary) colors.primary else colors.surface, RoundedCornerShape(20.dp)).border(1.dp, if (primary) colors.primary else colors.outlineVariant, RoundedCornerShape(20.dp)).clickable(onClick = onClick).padding(18.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
        Box(Modifier.size(48.dp).background(foreground.copy(alpha = .09f), RoundedCornerShape(15.dp)), contentAlignment = Alignment.Center) { UiIcon(icon, Modifier.size(26.dp), if (primary) foreground else colors.primary) }
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(5.dp)) { Text(title, fontWeight = FontWeight.Bold, fontSize = 17.sp, color = foreground); Text(subtitle, fontSize = 12.sp, lineHeight = 18.sp, color = foreground.copy(alpha = .7f)) }
        Text("›", fontSize = 25.sp, color = foreground)
    }
}
@Composable private fun UiIcon(kind: String, modifier: Modifier = Modifier, color: Color = MaterialTheme.colorScheme.primary) {
    Canvas(modifier) {
        val scale = size.width / 24f
        val stroke = Stroke(width = 1.7f * scale, cap = StrokeCap.Round)
        fun line(a: Float, b: Float, c: Float, d: Float) { drawLine(color, Offset(a * scale, b * scale), Offset(c * scale, d * scale), 1.7f * scale, StrokeCap.Round) }
        when (kind) {
            "terminal" -> { drawRoundRect(color, Offset(2 * scale, 3 * scale), androidx.compose.ui.geometry.Size(20 * scale, 18 * scale), androidx.compose.ui.geometry.CornerRadius(3 * scale), style = stroke); line(6f, 8f, 10f, 12f); line(10f, 12f, 6f, 16f); line(13f, 16f, 18f, 16f) }
            "desktop" -> { drawRoundRect(color, Offset(2 * scale, 3 * scale), androidx.compose.ui.geometry.Size(20 * scale, 14 * scale), androidx.compose.ui.geometry.CornerRadius(2 * scale), style = stroke); line(12f, 17f, 12f, 22f); line(7f, 22f, 17f, 22f) }
            "home" -> { val path = Path().apply { moveTo(2 * scale, 11 * scale); lineTo(12 * scale, 3 * scale); lineTo(22 * scale, 11 * scale); moveTo(5 * scale, 9 * scale); lineTo(5 * scale, 21 * scale); lineTo(19 * scale, 21 * scale); lineTo(19 * scale, 9 * scale); moveTo(9 * scale, 21 * scale); lineTo(9 * scale, 14 * scale); lineTo(15 * scale, 14 * scale); lineTo(15 * scale, 21 * scale) }; drawPath(path, color, style = stroke) }
            "settings" -> { drawCircle(color, 7.5f * scale, style = stroke); drawCircle(color, 2.8f * scale, style = stroke); for (i in 0..7) { val a = i * Math.PI / 4; line((12 + 8 * kotlin.math.cos(a)).toFloat(), (12 + 8 * kotlin.math.sin(a)).toFloat(), (12 + 10.5 * kotlin.math.cos(a)).toFloat(), (12 + 10.5 * kotlin.math.sin(a)).toFloat()) } }
            else -> { drawCircle(color, 8f * scale, style = stroke); drawCircle(color, 3f * scale, style = stroke) }
        }
    }
}
