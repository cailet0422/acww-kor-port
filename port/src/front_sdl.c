/* The port's front end: runtime/play.c (the decomp's Win32 window) redone on SDL2, so the same
 * run plays on Linux and Android.
 *
 *     acww-kor [options] [SAVE]
 *
 * As in play.c, the run is the decomp's headless one (run.c built with RUN_FRONT_END) and this
 * file only sets its environment and watches it through the hooks that are NULL in the
 * headless program: run_frame_hook (run.h) on the emulation's thread once per frame, and
 * frame_output_hook (frame.h) on the drawing thread with each finished frame. Unlike play.c,
 * the window lives on the emulation's thread: each frame the events are read, the last
 * finished frame is drawn, and the run is paced. Android needs that (SDL's video and events
 * belong to the thread SDL_main runs on), and a frame of drawing costs little next to a frame
 * of emulation.
 *
 * Before the run, the ROM is checked and split into the modules the loader reads
 * (rom_prep.c). Everything the port writes goes in its data directory: the app's files on
 * Android, SDL_GetPrefPath elsewhere (--data overrides it).
 *
 * Input: the keyboard as in play.c, the mouse or a finger on the lower screen as the stylus,
 * game controllers, and on a touch screen buttons drawn around the screens (touch.c).
 */
#include <SDL.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "arm7.h"
#include "frame.h"
#include "input.h"
#include "run.h"
#include "sound.h"

#include "front.h"
#include "game_hooks.h"
#include "rom_prep.h"
#include "tall3d.h"
#include "touch.h"

/* The DS's frame: 263 lines of 2130 bus cycles at 33,513,982 Hz, about 59.8261 a second. */
#define DS_CYCLES_PER_FRAME (263.0 * 2130.0)
#define DS_BUS_HZ 33513982.0

#define PATH_SIZE 1024

/* Options -------------------------------------------------------------------------------- */

static struct {
    char data[PATH_SIZE], rom[PATH_SIZE], save[PATH_SIZE], modules_dir[PATH_SIZE], manifest[PATH_SIZE];
    const char *rtc, *input;
    int read_only, scale, unpaced, linear, overlay, tall, full_touch;
    int no_above;        /* the game's own choice of what to draw above its screen (--no-above) */
    const char *hash;    /* a file for the frames' hashes, as RUN_HASH (--hash) */
    int width, height;   /* the window's starting size (--size), else by --scale */
    uint32_t window_shot; /* the frame whose window is saved as a PNG (--window-shot) */
    uint32_t stop_frame; /* 0: none */
    uint32_t shots[8];   /* frames to save as PNGs (--shot) */
    int shot_count;
} opt;

static void usage(void)
{
    fprintf(stderr,
            "usage: acww-kor [options] [SAVE]\n"
            "  SAVE            the save file (256 KB flash image); default DATA/acww.sav\n"
            "  --data DIR      where the port keeps the ROM's modules and the save (default: the user's\n"
            "                  data directory, %s)\n"
            "  --rom FILE      your Korean ROM (ADMK); default DATA/game.nds\n"
            "  --read-only     never write the save file\n"
            "  --rtc TIME      start the clock at \"YYYY-MM-DD HH:MM:SS\" (default: this machine's clock)\n"
            "  --input FILE    also play an input recording (decomp/runtime/input.h), under the live input\n"
            "  --scale N       the window's starting size: N times the DS's screens (default 2)\n"
            "  --frames N      save and quit after N frames (for scripted checks)\n"
            "  --fast          no pacing: as fast as this machine runs it (as with Tab held)\n"
            "  --smooth        scale the screens with linear filtering instead of whole pixels\n"
            "  --touch         draw the touch buttons even without a touch screen\n"
            "  --full-touch    start without the touch buttons (the stylus alone); the mark at the\n"
            "                  corner, or Back on Android, turns them on and off, and it is remembered\n"
            "  --size WxH      the window's starting size in pixels\n"
            "  --window-shot N save the window as it is drawn at frame N as a PNG in DATA\n"
            "  --two-screens   the DS's two pictures as they are, without the 3D scene continued\n"
            "                  onto the upper screen\n"
            "  --no-above      with the tall view, draw only the objects the game itself would\n"
            "  --hash FILE     write each frame's hashes there (the decomp's RUN_HASH), to compare runs\n"
            "  --shot N        save the screens of frame N as a PNG in DATA (may be given 8 times)\n",
            opt.data);
    exit(2);
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void default_data_dir(char *out)
{
#ifdef __ANDROID__
    const char *base = SDL_AndroidGetInternalStoragePath();
    snprintf(out, PATH_SIZE, "%s", base != NULL ? base : ".");
#else
    char *base = SDL_GetPrefPath("acww-kor-port", "acww-kor");
    snprintf(out, PATH_SIZE, "%s", base != NULL ? base : "./");
    SDL_free(base);
    size_t n = strlen(out);
    if (n > 1 && (out[n - 1] == '/' || out[n - 1] == '\\')) {
        out[n - 1] = 0;
    }
#endif
}

/* A message the player sees: a dialog where there is a screen to show it on, and the log. */
static void tell(const char *title, const char *text)
{
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", title, text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, NULL);
}

/* The log ------------------------------------------------------------------------------------ */

/* On Android nothing shows a program's stdout and stderr, where the runtime reports what it
 * does and why a run stops: both go to DATA/log.txt (the last run's as log-1.txt), and what
 * SDL_Log writes goes there too. When a run cannot start, the end of the file is what the
 * player is shown. */
static char log_path[PATH_SIZE + 16];

#ifdef __ANDROID__
static void log_to_file(void *unused, int category, SDL_LogPriority priority, const char *message)
{
    (void) unused;
    (void) category;
    fprintf(stderr, "%s%s\n", priority >= SDL_LOG_PRIORITY_ERROR ? "ERROR: " : "", message);
    fflush(stderr);
}
#endif

static void log_open(void)
{
#ifdef __ANDROID__
    char last[PATH_SIZE + 16];
    snprintf(log_path, sizeof(log_path), "%s/log.txt", opt.data);
    snprintf(last, sizeof(last), "%s/log-1.txt", opt.data);
    rename(log_path, last);
    if (freopen(log_path, "w", stderr) != NULL) {
        dup2(fileno(stderr), fileno(stdout));
        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stderr, NULL, _IOLBF, 0);
        SDL_LogSetOutputFunction(log_to_file, NULL);
    } else {
        log_path[0] = 0;
    }
#endif
}

/* the last `size - 1` bytes of the log, from the start of a line */
static void log_tail(char *out, size_t size)
{
    out[0] = 0;
    fflush(stdout);
    fflush(stderr);
    FILE *f = log_path[0] != 0 ? fopen(log_path, "rb") : NULL;
    if (f == NULL) {
        return;
    }
    fseek(f, 0, SEEK_END);
    long end = ftell(f), from = end > (long) size - 1 ? end - ((long) size - 1) : 0;
    fseek(f, from, SEEK_SET);
    size_t n = fread(out, 1, size - 1, f);
    fclose(f);
    out[n] = 0;
    char *line = from > 0 ? strchr(out, '\n') : NULL;
    if (line != NULL) {
        memmove(out, line + 1, strlen(line + 1) + 1);
    }
}

/* Shared state ------------------------------------------------------------------------------ */

/* The last frame drawn, upper screen above the lower one, as the drawing thread left it. */
static uint32_t shown[FRONT_SCREEN_W * FRONT_SCREEN_H * 2];
static uint32_t shown_frame, drawn_frame;
static SDL_mutex *shown_lock;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *screens;
static Layout layout;

static uint32_t keys_held_keyboard, keys_held_pad, keys_taps;
static int pen_down, pen_x, pen_y, pen_tap, pen_tap_x, pen_tap_y;
static int mouse_pen, fast_keyboard, quit_asked;
static SDL_GameController *pads[4];

/* The save file ----------------------------------------------------------------------------- */

static uint64_t save_writes_seen, save_writes_flushed;
static uint32_t save_quiet_frames;
static int save_backed_up;
static uint32_t frame_now;

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    FILE *out = in != NULL ? fopen(to, "wb") : NULL;
    int ok = in != NULL && out != NULL;
    char buf[65536];
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0) {
        ok = fwrite(buf, 1, n, out) == n;
    }
    if (in != NULL) {
        fclose(in);
    }
    if (out != NULL && fclose(out) != 0) {
        ok = 0;
    }
    return ok ? 0 : -1;
}

/* Writes the chip's image to the save file: to SAVE.tmp first, then in its place, so a crash
 * never leaves half a save. The first time an existing file is replaced, it is kept as
 * SAVE.bak (unless there is one). */
static void save_flush(const char *why)
{
    uint64_t writes;
    const uint8_t *image = arm7_backup(&writes);
    if (opt.read_only || image == NULL || writes == save_writes_flushed) {
        return;
    }
    if (!save_backed_up) {
        char bak[PATH_SIZE + 8];
        snprintf(bak, sizeof(bak), "%s.bak", opt.save);
        if (file_exists(opt.save) && !file_exists(bak)) {
            copy_file(opt.save, bak);
        }
        save_backed_up = 1;
    }
    char tmp[PATH_SIZE + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", opt.save);
    FILE *f = fopen(tmp, "wb");
    if (f == NULL || fwrite(image, 1, ARM7_BACKUP_SIZE, f) != ARM7_BACKUP_SIZE || fclose(f) != 0) {
        SDL_Log("play: cannot write %s", tmp);
        return;
    }
    if (rename(tmp, opt.save) != 0) {
        SDL_Log("play: cannot replace %s (%s)", opt.save, strerror(errno));
        return;
    }
    save_writes_flushed = writes;
    SDL_Log("play: frame %u: saved %s (%s, %llu writes to the chip)", frame_now, opt.save, why,
            (unsigned long long) writes);
}

static void at_exit(void)
{
    save_flush("at exit");
}

/* Settings ---------------------------------------------------------------------------------- */

/* What the player chose while playing, kept in DATA/settings.txt as "name=value" lines. */
static void settings_path(char *out, size_t size)
{
    snprintf(out, size, "%s/settings.txt", opt.data);
}

static void settings_load(void)
{
    char path[PATH_SIZE + 16], line[128];
    settings_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        int v;
        if (sscanf(line, "full_touch=%d", &v) == 1) {
            opt.full_touch = v != 0;
        }
    }
    fclose(f);
}

static void settings_save(void)
{
    char path[PATH_SIZE + 16];
    settings_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "full_touch=%d\n", opt.full_touch);
        fclose(f);
    }
}

/* The window -------------------------------------------------------------------------------- */

static int open_window(void)
{
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, opt.linear ? "1" : "0");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        tell("acww-kor", SDL_GetError());
        return -1;
    }
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    window = SDL_CreateWindow("acww-kor-port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              opt.width > 0 ? opt.width : FRONT_SCREEN_W * opt.scale,
                              opt.height > 0 ? opt.height : 2 * FRONT_SCREEN_H * opt.scale, flags);
    renderer = window != NULL ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (renderer == NULL && window != NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    screens = renderer != NULL ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING,
                                                   FRONT_SCREEN_W, 2 * FRONT_SCREEN_H)
                               : NULL;
    if (screens == NULL) {
        tell("acww-kor", SDL_GetError());
        return -1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return 0;
}

static void update_layout(void)
{
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    int touch = opt.overlay || SDL_GetNumTouchDevices() > 0;
    layout = layout_compute(w, h, touch, opt.full_touch);
}

/* The picture enlarged for the window. A phone's screen is rarely a whole multiple of the
 * DS's, and neither way of scaling by a fraction looks right: the nearest pixel makes them
 * uneven, a linear filter blurs them. So, unless --smooth asks for the plain linear filter,
 * the picture is first enlarged by a whole number with its pixels kept square (into `sharp`),
 * and that is taken to the window's size with the linear filter, which then only softens the
 * pixels' edges. */
static SDL_Texture *sharp;
static int sharp_factor;

static SDL_Texture *enlarged(const SDL_Rect *to)
{
    int factor = (to->w + FRONT_SCREEN_W - 1) / FRONT_SCREEN_W;
    factor = factor < 1 ? 1 : factor > 4 ? 4 : factor;
    if (opt.linear || to->w == FRONT_SCREEN_W * factor || !SDL_RenderTargetSupported(renderer)) {
        return screens; /* linear was asked for, or the size is a whole multiple: as it is */
    }
    if (sharp == NULL || sharp_factor != factor) {
        if (sharp != NULL) {
            SDL_DestroyTexture(sharp);
        }
        sharp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, FRONT_SCREEN_W * factor,
                                  2 * FRONT_SCREEN_H * factor);
        sharp_factor = factor;
        if (sharp == NULL) {
            return screens;
        }
        SDL_SetTextureScaleMode(sharp, SDL_ScaleModeLinear);
    }
    SDL_SetRenderTarget(renderer, sharp);
    SDL_RenderCopy(renderer, screens, NULL, NULL);
    SDL_SetRenderTarget(renderer, NULL);
    return sharp;
}

static void window_shot(void)
{
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    uint32_t *pixels = malloc((size_t) w * (size_t) h * 4);
    char path[PATH_SIZE + 64];
    snprintf(path, sizeof(path), "%s/window-%05u.png", opt.data, frame_now);
    if (pixels != NULL && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB888, pixels, w * 4) == 0) {
        png_write(path, pixels, w, h);
    }
    free(pixels);
}

static void draw(void)
{
    if (shown_frame != drawn_frame) {
        SDL_LockMutex(shown_lock);
        SDL_UpdateTexture(screens, NULL, shown, FRONT_SCREEN_W * (int) sizeof(uint32_t));
        drawn_frame = shown_frame;
        SDL_UnlockMutex(shown_lock);
    }
    update_layout();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_Rect picture = {layout.top.x, layout.top.y, layout.top.w, layout.top.h + layout.bottom.h};
    SDL_Texture *from = enlarged(&picture);
    SDL_RenderCopy(renderer, from, NULL, &picture);
    if (layout.touch) {
        touch_draw(renderer, &layout, keys_held_keyboard | keys_held_pad | touch_keys());
    }
    if (opt.window_shot != 0 && opt.window_shot == frame_now) {
        window_shot();
    }
    SDL_RenderPresent(renderer);
}

/* The pen at a window position (in renderer pixels) on the lower screen; clamped there while
 * dragging. Returns whether the position is on it. */
static int pen_at(int x, int y, int clamp, int *px, int *py)
{
    const SDL_Rect *r = &layout.bottom;
    if (r->w <= 0 || r->h <= 0) {
        return 0;
    }
    int sx = (int) ((int64_t) (x - r->x) * FRONT_SCREEN_W / r->w);
    int sy = (int) ((int64_t) (y - r->y) * FRONT_SCREEN_H / r->h);
    int inside = x >= r->x && y >= r->y && sx >= 0 && sx < FRONT_SCREEN_W && sy >= 0 && sy < FRONT_SCREEN_H;
    if (!inside && !clamp) {
        return 0;
    }
    *px = sx < 0 ? 0 : sx >= FRONT_SCREEN_W ? FRONT_SCREEN_W - 1 : sx;
    *py = sy < 0 ? 0 : sy >= FRONT_SCREEN_H ? FRONT_SCREEN_H - 1 : sy;
    return inside;
}

/* window coordinates (mouse) to renderer pixels, which differ on high-DPI screens */
static void mouse_to_pixels(int *x, int *y)
{
    int ww, wh, pw, ph;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetRendererOutputSize(renderer, &pw, &ph);
    if (ww > 0 && wh > 0) {
        *x = *x * pw / ww;
        *y = *y * ph / wh;
    }
}

static void pen_set(int down, int x, int y)
{
    if (down && !pen_down) {
        pen_tap = 1;
        pen_tap_x = x;
        pen_tap_y = y;
    }
    pen_down = down;
    if (down) {
        pen_x = x;
        pen_y = y;
    }
}

static void toggle_buttons(void)
{
    opt.full_touch = !opt.full_touch;
    settings_save();
}

static uint32_t key_of(SDL_Keycode k)
{
    switch (k) {
        case SDLK_UP:
            return DS_KEY_UP;
        case SDLK_DOWN:
            return DS_KEY_DOWN;
        case SDLK_LEFT:
            return DS_KEY_LEFT;
        case SDLK_RIGHT:
            return DS_KEY_RIGHT;
        case SDLK_x:
            return DS_KEY_A;
        case SDLK_z:
            return DS_KEY_B;
        case SDLK_s:
            return DS_KEY_X;
        case SDLK_a:
            return DS_KEY_Y;
        case SDLK_q:
            return DS_KEY_L;
        case SDLK_w:
            return DS_KEY_R;
        case SDLK_RETURN:
            return DS_KEY_START;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
        case SDLK_BACKSPACE:
            return DS_KEY_SELECT;
        default:
            return 0;
    }
}

/* Game controllers: the DS's face buttons by position (SDL names them as on an Xbox pad, so
 * the right one is B and the bottom one A), the d-pad and the left stick as the +Control Pad. */
static uint32_t pad_keys(void)
{
    static const struct {
        SDL_GameControllerButton button;
        uint32_t key;
    } map[] = {
        {SDL_CONTROLLER_BUTTON_B, DS_KEY_A},           {SDL_CONTROLLER_BUTTON_A, DS_KEY_B},
        {SDL_CONTROLLER_BUTTON_Y, DS_KEY_X},           {SDL_CONTROLLER_BUTTON_X, DS_KEY_Y},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, DS_KEY_L}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, DS_KEY_R},
        {SDL_CONTROLLER_BUTTON_START, DS_KEY_START},   {SDL_CONTROLLER_BUTTON_BACK, DS_KEY_SELECT},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, DS_KEY_UP},    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, DS_KEY_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, DS_KEY_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, DS_KEY_RIGHT},
    };
    uint32_t keys = 0;
    for (int i = 0; i < 4; i++) {
        if (pads[i] == NULL) {
            continue;
        }
        for (size_t m = 0; m < sizeof(map) / sizeof(map[0]); m++) {
            if (SDL_GameControllerGetButton(pads[i], map[m].button)) {
                keys |= map[m].key;
            }
        }
        const int dead = 16000;
        int x = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTX);
        int y = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTY);
        keys |= x < -dead ? DS_KEY_LEFT : x > dead ? DS_KEY_RIGHT : 0;
        keys |= y < -dead ? DS_KEY_UP : y > dead ? DS_KEY_DOWN : 0;
        if (SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_TRIGGERLEFT) > dead) {
            keys |= DS_KEY_L;
        }
        if (SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > dead) {
            keys |= DS_KEY_R;
        }
    }
    return keys;
}

static void screenshot(void)
{
    static uint32_t copy[FRONT_SCREEN_W * FRONT_SCREEN_H * 2];
    SDL_LockMutex(shown_lock);
    memcpy(copy, shown, sizeof(copy));
    SDL_UnlockMutex(shown_lock);
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char path[PATH_SIZE + 64];
    snprintf(path, sizeof(path), "%s/acww-%04d%02d%02d-%02d%02d%02d.png", opt.data, t->tm_year + 1900, t->tm_mon + 1,
             t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    SDL_Log(png_write(path, copy, FRONT_SCREEN_W, 2 * FRONT_SCREEN_H) == 0 ? "play: screenshot %s" : "play: cannot write %s",
            path);
}

static void handle(const SDL_Event *e)
{
    int x, y;
    switch (e->type) {
        case SDL_QUIT:
            quit_asked = 1;
            break;
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_TERMINATING:
            save_flush("the app went to the background");
            break;
        case SDL_KEYDOWN:
            if (e->key.keysym.sym == SDLK_TAB) {
                fast_keyboard = 1;
            } else if (e->key.keysym.sym == SDLK_AC_BACK && !e->key.repeat) {
                toggle_buttons(); /* Android's Back */
            } else if (e->key.keysym.sym == SDLK_F12 && !e->key.repeat) {
                screenshot();
            } else {
                uint32_t k = key_of(e->key.keysym.sym);
                keys_held_keyboard |= k;
                keys_taps |= k;
            }
            break;
        case SDL_KEYUP:
            if (e->key.keysym.sym == SDLK_TAB) {
                fast_keyboard = 0;
            } else {
                keys_held_keyboard &= ~key_of(e->key.keysym.sym);
            }
            break;
        case SDL_WINDOWEVENT:
            if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                keys_held_keyboard = 0;
                fast_keyboard = 0;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e->button.button == SDL_BUTTON_LEFT) {
                x = e->button.x;
                y = e->button.y;
                mouse_to_pixels(&x, &y);
                if (pen_at(x, y, 0, &x, &y)) {
                    mouse_pen = 1;
                    pen_set(1, x, y);
                }
            }
            break;
        case SDL_MOUSEMOTION:
            if (mouse_pen) {
                x = e->motion.x;
                y = e->motion.y;
                mouse_to_pixels(&x, &y);
                pen_at(x, y, 1, &x, &y);
                pen_set(1, x, y);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e->button.button == SDL_BUTTON_LEFT && mouse_pen) {
                mouse_pen = 0;
                pen_set(0, 0, 0);
            }
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP: {
            int w, h;
            SDL_GetRendererOutputSize(renderer, &w, &h);
            x = (int) (e->tfinger.x * (float) w);
            y = (int) (e->tfinger.y * (float) h);
            TouchPen pen = touch_finger(&layout, e->type == SDL_FINGERDOWN ? TOUCH_DOWN
                                                 : e->type == SDL_FINGERUP ? TOUCH_UP
                                                                           : TOUCH_MOVE,
                                        (int64_t) e->tfinger.fingerId, x, y);
            if (pen.toggle) {
                toggle_buttons();
            }
            if (pen.changed) {
                int px = 0, py = 0;
                if (pen.down) {
                    pen_at(pen.x, pen.y, 1, &px, &py);
                }
                pen_set(pen.down, px, py);
            }
            keys_taps |= touch_taps();
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
            for (int i = 0; i < 4; i++) {
                if (pads[i] == NULL) {
                    pads[i] = SDL_GameControllerOpen(e->cdevice.which);
                    break;
                }
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            for (int i = 0; i < 4; i++) {
                if (pads[i] != NULL &&
                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == e->cdevice.which) {
                    SDL_GameControllerClose(pads[i]);
                    pads[i] = NULL;
                }
            }
            break;
        default:
            break;
    }
}

/* The drawing thread's frames ---------------------------------------------------------------- */

/* The 3D scene's upper part (tall3d.h) over the upper screen: opaque where the scene is,
 * blended at its translucent pixels and antialiased edges, nothing where it drew nothing. */
static void lay_tall(uint32_t *upper)
{
    static uint32_t tall[TALL3D_H][TALL3D_W];
    if (!tall3d_frame(tall)) {
        return;
    }
    for (int y = 0; y < TALL3D_H; y++) {
        for (int x = 0; x < TALL3D_W; x++) {
            uint32_t p = tall[y][x], a = p >> 24;
            if (a == 0) {
                continue;
            }
            uint32_t *to = &upper[y * FRONT_SCREEN_W + x], out = 0;
            for (int k = 0; k < 3; k++) { /* red, green, blue: 6 bits here, 8 on the screen */
                uint32_t c = (p >> (8 * k)) & 63, old = (*to >> (16 - 8 * k)) & 255;
                c = c << 2 | c >> 4;
                out |= (a >= 31 ? c : (c * a + old * (31 - a)) / 31) << (16 - 8 * k);
            }
            *to = out;
        }
    }
}

static void on_frame_output(uint32_t frame, const uint32_t *upper, const uint32_t *lower)
{
    SDL_LockMutex(shown_lock);
    memcpy(shown, upper, sizeof(uint32_t) * FRONT_SCREEN_W * FRONT_SCREEN_H);
    memcpy(shown + FRONT_SCREEN_W * FRONT_SCREEN_H, lower, sizeof(uint32_t) * FRONT_SCREEN_W * FRONT_SCREEN_H);
    if (opt.tall) {
        lay_tall(shown);
    }
    shown_frame = frame + 1;
    for (int i = 0; i < opt.shot_count; i++) {
        if (opt.shots[i] == frame) {
            char path[PATH_SIZE + 64];
            snprintf(path, sizeof(path), "%s/frame-%05u.png", opt.data, frame);
            png_write(path, shown, FRONT_SCREEN_W, 2 * FRONT_SCREEN_H);
        }
    }
    SDL_UnlockMutex(shown_lock);
}

/* The pacer --------------------------------------------------------------------------------- */

static double ticks_per_frame, next_tick, counter_hz;
static uint64_t busy_ticks, stats_start, stats_frame_start;
static uint32_t stats_frames;

/* Sleeps until a millisecond before the frame's moment, then spins. */
static void wait_until(double tick)
{
    double left = tick - (double) SDL_GetPerformanceCounter();
    double ms = counter_hz / 1000.0;
    if (left > ms * 2.0) {
        SDL_Delay((Uint32) ((left - ms) / ms));
    }
    while ((double) SDL_GetPerformanceCounter() < tick) {
    }
}

static void pace(void)
{
    int fast = fast_keyboard || opt.unpaced;
    sound_fast(fast);
    uint64_t start = SDL_GetPerformanceCounter();
    if (next_tick == 0) {
        next_tick = (double) start;
        stats_start = stats_frame_start = start;
    }
    next_tick += ticks_per_frame;
    if (fast || (double) start > next_tick + 4 * ticks_per_frame) {
        next_tick = (double) start; /* fast forward, or too far behind: no catching up */
    } else {
        wait_until(next_tick);
    }
    uint64_t end = SDL_GetPerformanceCounter();
    busy_ticks += start - stats_frame_start;
    stats_frame_start = end;
    stats_frames++;
    double seconds = (double) (end - stats_start) / counter_hz;
    if (seconds >= 5.0) {
        double fps = stats_frames / seconds;
        double busy = (double) busy_ticks / (double) (end - stats_start);
        SDL_Log("play: frame %u, %.2f fps (the DS: %.2f), emulation busy %.0f%% of the time%s", frame_now, fps,
                DS_BUS_HZ / DS_CYCLES_PER_FRAME, busy * 100.0, fast ? ", fast forward" : "");
        char title[128];
        snprintf(title, sizeof(title), "acww-kor-port - %.1f fps", fps);
        SDL_SetWindowTitle(window, title);
        stats_start = end;
        stats_frames = 0;
        busy_ticks = 0;
    }
}

/* Once per frame, on the emulation's thread ------------------------------------------------ */

static int frame_first = 1;
static uint32_t taps_last;
static int pen_tap_last, pen_tap_last_x, pen_tap_last_y;
static uint32_t quit_wait;

static void on_frame(uint32_t frame)
{
    frame_now = frame;
    if (frame_first) {
        /* the port's hooks into the game's code, behind the run's own, which exist by now */
        port_hooks_install();
        game_hooks_draw_above(opt.tall && !opt.no_above);
        frame_first = 0;
    }
    if (window == NULL) {
        /* the window opens here, after native_reserve has kept the DS's address ranges from
           the host's allocations (run.c), as play.c's does */
        if (open_window() != 0) {
            exit(1);
        }
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        handle(&e);
    }

    /* the input for this frame: what is held, and presses (and touches) that began and ended
       since the last frame, held for two frames so the game's reader sees them */
    uint32_t taps = keys_taps;
    keys_taps = 0;
    keys_held_pad = pad_keys();
    uint32_t keys = keys_held_keyboard | keys_held_pad | touch_keys() | taps | taps_last;
    taps_last = taps;
    int down = pen_down, x = pen_x, y = pen_y;
    if (!down && (pen_tap || pen_tap_last)) {
        x = pen_tap ? pen_tap_x : pen_tap_last_x;
        y = pen_tap ? pen_tap_y : pen_tap_last_y;
        down = 1;
    }
    pen_tap_last = pen_tap;
    pen_tap_last_x = pen_tap_x;
    pen_tap_last_y = pen_tap_y;
    pen_tap = 0;
    input_live(keys, down, x, y);

    /* the save: written once the game has not written to the chip for half a second */
    uint64_t writes;
    arm7_backup(&writes);
    if (writes != save_writes_seen) {
        save_writes_seen = writes;
        save_quiet_frames = 0;
    } else if (writes != save_writes_flushed && ++save_quiet_frames >= 30) {
        save_flush("the game saved");
    }

    /* quitting: after the game's writes to the chip have settled (at most 3 seconds more) */
    if (quit_asked || (opt.stop_frame && frame >= opt.stop_frame)) {
        if (writes == save_writes_flushed || save_quiet_frames >= 30 || ++quit_wait > 180) {
            save_flush(quit_asked ? "quit" : "frame limit");
            SDL_Log("play: %lu drawings of objects above the game's screen", game_hooks_drawn_above());
            SDL_Log("play: ended at frame %u", frame);
            /* not exit(): the drawing thread is still running, and exit() takes apart what it
               uses (Android's C library aborts on a mutex destroyed under a thread) */
            fflush(NULL);
            _exit(0);
        }
    }
    /* fast forward: the window every tenth frame is enough, and the drawing would be the
       slowest part of it */
    if (!(fast_keyboard || opt.unpaced) || frame % 10 == 0 || frame == opt.window_shot) {
        draw();
    }
    pace();
}

/* main -------------------------------------------------------------------------------------- */

static void set_env(const char *name, const char *value)
{
    SDL_setenv(name, value != NULL ? value : "", 1);
}

static const char *rom_problem(RomStatus status)
{
    switch (status) {
        case ROM_MISSING:
            return "ROM 파일이 없습니다. / No ROM file.";
        case ROM_UNREADABLE:
            return "ROM 파일을 읽을 수 없습니다. / The ROM file cannot be read.";
        case ROM_NOT_DS:
            return "DS ROM이 아닙니다. / This is not a DS ROM.";
        case ROM_OTHER_VERSION:
            return "다른 게임이거나 다른 지역판입니다. 한국판(ADMK)이 필요합니다.\n"
                   "Another game, region or revision: this port needs the Korean release (ADMK).";
        case ROM_BAD_DUMP:
            return "한국판이지만 내용이 다릅니다(잘못된 덤프이거나 패치된 ROM).\n"
                   "The Korean release, but different bytes (a bad dump or a patched ROM).";
        case ROM_WRITE_FAILED:
            return "ROM에서 꺼낸 코드를 저장할 수 없습니다. / The game's code could not be written out.";
        default:
            return "";
    }
}

int main(int argc, char **argv)
{
    opt.scale = 2;
    opt.tall = 1;
    default_data_dir(opt.data);
    const char *save = NULL, *rom = NULL;
    int full_touch = -1; /* as the settings say, unless the command line does */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (strcmp(a, "--read-only") == 0) {
            opt.read_only = 1;
        } else if (strcmp(a, "--fast") == 0) {
            opt.unpaced = 1;
        } else if (strcmp(a, "--smooth") == 0) {
            opt.linear = 1;
        } else if (strcmp(a, "--touch") == 0) {
            opt.overlay = 1;
        } else if (strcmp(a, "--no-above") == 0) {
            opt.no_above = 1;
        } else if (strcmp(a, "--hash") == 0 && more) {
            opt.hash = argv[++i];
        } else if (strcmp(a, "--full-touch") == 0) {
            full_touch = 1;
        } else if (strcmp(a, "--size") == 0 && more) {
            sscanf(argv[++i], "%dx%d", &opt.width, &opt.height);
        } else if (strcmp(a, "--window-shot") == 0 && more) {
            opt.window_shot = (uint32_t) strtoul(argv[++i], NULL, 10);
        } else if (strcmp(a, "--two-screens") == 0) {
            opt.tall = 0;
        } else if (strcmp(a, "--shot") == 0 && more && opt.shot_count < 8) {
            opt.shots[opt.shot_count++] = (uint32_t) strtoul(argv[++i], NULL, 10);
        } else if (strcmp(a, "--rtc") == 0 && more) {
            opt.rtc = argv[++i];
        } else if (strcmp(a, "--input") == 0 && more) {
            opt.input = argv[++i];
        } else if (strcmp(a, "--scale") == 0 && more) {
            opt.scale = atoi(argv[++i]);
        } else if (strcmp(a, "--frames") == 0 && more) {
            opt.stop_frame = (uint32_t) strtoul(argv[++i], NULL, 10);
        } else if (strcmp(a, "--data") == 0 && more) {
            snprintf(opt.data, sizeof(opt.data), "%s", argv[++i]);
        } else if (strcmp(a, "--rom") == 0 && more) {
            rom = argv[++i];
        } else if (a[0] == '-' || save != NULL) {
            usage();
        } else {
            save = a;
        }
    }
    if (opt.scale < 1 || opt.scale > 8) {
        opt.scale = 2;
    }
    mkdir(opt.data, 0700);
    log_open();
    settings_load();
    if (full_touch >= 0) {
        opt.full_touch = full_touch;
    }
    SDL_Log("acww-kor-port %s, %d-bit", PORT_VERSION, (int) (8 * sizeof(void *)));
    snprintf(opt.rom, sizeof(opt.rom), "%s", rom != NULL ? rom : "");
    if (rom == NULL) {
        snprintf(opt.rom, sizeof(opt.rom), "%s/game.nds", opt.data);
    }
    snprintf(opt.save, sizeof(opt.save), "%s", save != NULL ? save : "");
    if (save == NULL) {
        snprintf(opt.save, sizeof(opt.save), "%s/acww.sav", opt.data);
    }
    snprintf(opt.modules_dir, sizeof(opt.modules_dir), "%s/modules", opt.data);

    char detail[512], rom_used[PATH_SIZE];
    RomStatus status = rom_prepare(opt.rom, opt.modules_dir, 0, rom_used, sizeof(rom_used), opt.manifest,
                                   sizeof(opt.manifest), detail, sizeof(detail));
    if (status != ROM_OK) {
        char text[1024];
        snprintf(text, sizeof(text), "%s\n\n%s", rom_problem(status), detail);
        tell("acww-kor-port", text);
#ifdef __ANDROID__
        /* the app's own copy of a ROM that cannot be played: removed, so the next start asks
           for another (LaunchActivity) */
        if (status == ROM_OTHER_VERSION || status == ROM_BAD_DUMP || status == ROM_NOT_DS) {
            remove(opt.rom);
        }
#endif
        return 1;
    }

    /* the run's environment, as the decomp's tools/play.py and play.c set it: the Reference
       form (every function as the ROM's code), docs/roadmap.md */
    set_env("RUN_NATIVE", "0");
    set_env("RUN_NATIVE_POLICY", "");
    set_env("RUN_SAVE", file_exists(opt.save) ? opt.save : "");
    set_env("RUN_RTC", opt.rtc);
    set_env("RUN_INPUT", opt.input);
    set_env("RUN_AUDIO", "");
    set_env("RUN_AUDIO_WAV", "");
    set_env("RUN_HASH", opt.hash);
    static const char *const cleared[] = {"RUN_PNG",           "RUN_PNG_FRAMES",      "RUN_DUMP3D",   "RUN_NATIVE_RECORD",
                                          "RUN_NATIVE_REPLAY", "RUN_RAM_HASH", "RUN_RAM_DUMP", "RUN_NATIVE_LEARN",
                                          "RUN_CLEAN_RAN",     "RUN_TRACE",    "RUN_CALLS",    "RUN_DRIVE"};
    for (size_t i = 0; i < sizeof(cleared) / sizeof(cleared[0]); i++) {
        set_env(cleared[i], "");
    }
#ifdef __ANDROID__
    /* where port_compat.h's memfd_create stand-in makes its file on old kernels */
    char tmp[PATH_SIZE];
    snprintf(tmp, sizeof(tmp), "%s/tmp", opt.data);
    mkdir(tmp, 0700);
    set_env("TMPDIR", tmp);
#endif

    SDL_Log("play: Reference, ROM %s, save %s%s%s", opt.rom, opt.save, file_exists(opt.save) ? "" : " (none yet: a new game)",
            opt.read_only ? ", read only" : "");
    SDL_Log("play: keys: arrows = +Control Pad, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = START,");
    SDL_Log("      Shift or Backspace = SELECT, mouse on the lower screen = touch, Tab = fast forward, F12 = screenshot");

    tall3d_enable(opt.tall);
    shown_lock = SDL_CreateMutex();
    counter_hz = (double) SDL_GetPerformanceFrequency();
    ticks_per_frame = counter_hz * DS_CYCLES_PER_FRAME / DS_BUS_HZ;
    run_frame_hook = on_frame;
    frame_output_hook = on_frame_output;
    atexit(at_exit);

    char symbols[] = "";
    char instructions[] = "0";
    char *run_argv[] = {argv[0], rom_used, opt.manifest, symbols, instructions, NULL};
    int code = run_main(5, run_argv);
    char text[1600] = "게임을 시작하지 못했습니다. / The run could not start.\n\n";
    log_tail(text + strlen(text), 1200);
    tell("acww-kor-port", text);
    return code;
}
