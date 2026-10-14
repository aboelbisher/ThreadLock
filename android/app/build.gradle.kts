// Android build for Threadlock. Gradle runs the same CMake project as desktop (../../CMakeLists.txt)
// once per CPU architecture and packs libthreadlock.so into the APK together with ../../assets.
// The only Kotlin is the engine's native bridge and ThreadlockActivity; all game code is C++.
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.aboelbisher.threadlock"
    compileSdk = 36
    buildToolsVersion = "36.0.0"
    ndkVersion = "30.0.16248370"           // the NDK installed in Android Studio's SDK Manager

    defaultConfig {
        applicationId = "com.aboelbisher.threadlock"
        minSdk = 24                         // Android 7.0+
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"

        ndk {
            // arm64-v8a: real phones. x86_64: the emulator on a Windows/Intel/AMD PC.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "4.1.2"               // the CMake installed in Android Studio's SDK Manager
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDirs("../../assets")  // same assets folder the desktop build uses
            java.srcDirs(
                "../../external/WLWengine/engine/wlw/platform/android/java",  // the native bridge's Kotlin half
                "../../external/WLWengine/third_party/swappy/java")  // the engine's frame pacing (Swappy)
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}
