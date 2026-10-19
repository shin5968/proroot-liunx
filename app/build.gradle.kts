plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}
android {
    namespace = "dev.proroot.app"
    compileSdk = 35
    ndkVersion = "27.2.12479018"
    defaultConfig {
        applicationId = "dev.proroot.app"
        minSdk = 26
        // Prototype compatibility: rootfs executables in app data require this legacy target.
        targetSdk = 28
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild { cmake { cppFlags += "-std=c++17" } }
    }
    // Sideload-only prototype: retain this finding as a warning in the lint report.
    lint { warning += "ExpiredTargetSdkVersion" }
    buildFeatures { compose = true }
    packaging { jniLibs { useLegacyPackaging = true } }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}
dependencies {
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui")
}
