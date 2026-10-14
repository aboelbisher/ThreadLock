# R8 rules for the release build.
#
# The engine's C++ finds these by name over JNI (android_platform.cpp: the class through the class
# loader, WlwBridge.call / nativePost by name and signature), so R8 must not rename or remove
# them. The activity is named in the manifest (kept automatically), but its services come from
# the game's code.
-keep class com.wlw.WlwBridge { *; }
-keep class com.wlw.WlwActivity { *; }
-keep class com.wlw.WlwService { *; }
-keep class * implements com.wlw.WlwService { *; }
