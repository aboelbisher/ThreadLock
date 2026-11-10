// Android build for Threadlock. Gradle runs the same CMake project as desktop (../../CMakeLists.txt)
// once per CPU architecture and packs libthreadlock.so into the APK together with ../../assets.
// The only Kotlin is the native bridge (engine + kit) and ThreadlockActivity, which lists the SDK
// services the game uses (ads); all game code is C++.
import java.util.Properties

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
        applicationId = "com.shmoffeestudios.threadlock"  // permanent once on Play (the Shmoffee Studios account, like Bluzzle)
        minSdk = 24                         // Android 7.0+
        targetSdk = 36
        versionCode = 1                     // +1 for every upload to Play (it must always grow)
        versionName = "1.0"               // what players see

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
                "../../external/WLWengine/kit/android/java",                  // the kit's services (AdMob)
                "../../external/WLWengine/third_party/swappy/java")  // the engine's frame pacing (Swappy)
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // Release signing: the upload key's details live in android/keystore.properties, which is NOT
    // in git (see .gitignore), next to the keystore file itself:
    //   storeFile=C:/Users/<you>/keys/threadlock-upload.jks
    //   storePassword=...
    //   keyAlias=upload
    //   keyPassword=...
    // Without that file, release builds are signed with the debug key: fine for testing on a
    // phone, refused by Google Play.
    val keystoreFile = rootProject.file("keystore.properties")
    signingConfigs {
        if (keystoreFile.exists()) {
            val keystore = Properties().apply { keystoreFile.inputStream().use { load(it) } }
            create("upload") {
                storeFile = file(keystore.getProperty("storeFile"))
                storePassword = keystore.getProperty("storePassword")
                keyAlias = keystore.getProperty("keyAlias")
                keyPassword = keystore.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            // R8 shrinks and obfuscates the Kotlin/Java side (ours is tiny; the SDKs are not).
            // Classes that native code calls by name are kept in proguard-rules.pro.
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("upload") ?: signingConfigs.getByName("debug")
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("com.google.android.gms:play-services-ads:24.4.0")             // AdMob
    implementation("com.google.android.ump:user-messaging-platform:3.2.0")       // consent (GDPR...)
}
