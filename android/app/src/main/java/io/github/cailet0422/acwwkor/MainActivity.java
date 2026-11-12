package io.github.cailet0422.acwwkor;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.InputMethodManager;

import org.libsdl.app.SDLActivity;

/** The game: SDL's activity, which loads libSDL2.so and libmain.so (port/CMakeLists.txt) and
 *  runs port/src/front_sdl.c's main on its own thread. The ROM is the copy LaunchActivity made
 *  in the app's files; the front end finds it there (SDL_AndroidGetInternalStoragePath). */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    /** Whether the game is running in this process (LaunchActivity asks before it offers the
     *  autosave: a running game's files are its own). */
    static volatile boolean running;

    private AlertDialog exitDialog;

    @Override
    protected void onCreate(Bundle saved) {
        running = true;
        super.onCreate(saved);
    }

    @Override
    protected void onDestroy() {
        running = false;
        super.onDestroy();
    }

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

    /** Called by the game (front_sdl.c, typer_covered) from its own thread while the game's
     *  soft keyboard is up: how many pixels of the game's picture, from its bottom, the phone's
     *  keyboard covers (0 without one). The game then shows the screen being typed on in the
     *  part that is left. */
    public int keyboardCover() {
        View surface = mSurface;
        if (surface == null) {
            return 0;
        }
        int[] at = new int[2];
        surface.getLocationOnScreen(at);
        Rect seen = new Rect();
        surface.getWindowVisibleDisplayFrame(seen);
        int visible = seen.bottom;
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsets insets = surface.getRootWindowInsets();
            if (insets != null) {
                int ime = insets.getInsets(WindowInsets.Type.ime()).bottom;
                if (ime > 0) {
                    visible = Math.min(visible, surface.getRootView().getHeight() - ime);
                }
            }
        }
        return Math.max(0, at[1] + surface.getHeight() - visible);
    }

    /** Called by the game (front_sdl.c, typer_begin) from its own thread: the phone's keyboard
     *  begins anew, with no text of its own and no syllable half typed (the game's text was
     *  changed by other means, or has no room for what was typed). */
    public void restartTextInput() {
        runOnUiThread(() -> {
            InputMethodManager manager = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (manager != null && mTextEdit != null) {
                manager.restartInput(mTextEdit);
            }
        });
    }

    /** Called by the game (front_sdl.c, open_settings) from its own thread. */
    public void openSettings() {
        runOnUiThread(() -> startActivity(new Intent(this, SettingsActivity.class)));
    }
}
