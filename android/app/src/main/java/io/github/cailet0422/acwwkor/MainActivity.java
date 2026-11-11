package io.github.cailet0422.acwwkor;

import org.libsdl.app.SDLActivity;

/** The game: SDL's activity, which loads libSDL2.so and libmain.so (port/CMakeLists.txt) and
 *  runs port/src/front_sdl.c's main on its own thread. The ROM is the copy LaunchActivity made
 *  in the app's files; the front end finds it there (SDL_AndroidGetInternalStoragePath). */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }
}
