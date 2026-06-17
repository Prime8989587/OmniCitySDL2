package com.cristiverse.game;

import org.libsdl.app.SDLActivity;

/**
 * Entry activity for the CristiVerse Android port.
 *
 * All gameplay lives in the native C++ library (libmain.so); this class only
 * tells SDL which shared libraries to load. SDL handles the GL surface, the
 * event pump, immersive fullscreen, and the lifecycle bridge to native code.
 */
public class CristiVerseActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "main"
        };
    }
}
