package io.github.cailet0422.acwwkor;

import android.app.AlertDialog;
import android.content.Intent;

import org.libsdl.app.SDLActivity;

/** The game: SDL's activity, which loads libSDL2.so and libmain.so (port/CMakeLists.txt) and
 *  runs port/src/front_sdl.c's main on its own thread. The ROM is the copy LaunchActivity made
 *  in the app's files; the front end finds it there (SDL_AndroidGetInternalStoragePath). */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    private AlertDialog exitDialog;

    /** Called by the game (front_sdl.c, back_pressed) from its own thread when Back is pressed:
     *  asks, and on a yes has the game write its save file and end (SDL_QUIT). */
    public void confirmExit() {
        runOnUiThread(() -> {
            if (isFinishing() || (exitDialog != null && exitDialog.isShowing())) {
                return;
            }
            exitDialog = new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                    .setMessage(R.string.exit_ask)
                    .setPositiveButton(R.string.exit_yes, (d, w) -> SDLActivity.nativeSendQuit())
                    .setNegativeButton(android.R.string.cancel, null)
                    .show();
        });
    }

    /** Called by the game (front_sdl.c, open_settings) from its own thread. */
    public void openSettings() {
        runOnUiThread(() -> startActivity(new Intent(this, SettingsActivity.class)));
    }
}
