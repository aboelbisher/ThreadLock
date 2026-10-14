// Top-level build file. The app module is in app/; the game itself is the shared C++ in ../src.
plugins {
    id("com.android.application") version "8.11.1" apply false
    id("org.jetbrains.kotlin.android") version "2.1.21" apply false  // the native bridge's Kotlin side
}
