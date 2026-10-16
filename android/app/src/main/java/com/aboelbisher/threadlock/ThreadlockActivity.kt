package com.aboelbisher.threadlock

import com.wlw.WlwActivity
import com.wlw.kit.AdMobService

// The game's activity: the engine's NativeActivity (which runs the C++ game) plus the SDK services
// Threadlock reaches through the native bridge (ads).
class ThreadlockActivity : WlwActivity() {
    override fun createServices() = listOf(AdMobService(this))
}
