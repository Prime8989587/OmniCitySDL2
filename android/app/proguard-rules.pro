# Keep the SDL Java glue — it is referenced from native code via JNI, so the
# shrinker cannot see those usages and would otherwise strip them.
-keep class org.libsdl.app.** { *; }
-keep class com.cristiverse.game.** { *; }
-keepclassmembers class org.libsdl.app.** { *; }
