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
#include <SDL_vulkan.h>
#ifdef __ANDROID__
#include <SDL_syswm.h>
#endif
#ifdef __ANDROID__
#include <jni.h>
#endif

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

#include "autosave.h"
#include "front.h"
#include "game_hooks.h"
#include "mix2d.h"
#include "netlink.h"
#include "render.h"
#include "rom_prep.h"
#include "scene_file.h"
#include "scene_tween.h"
#include "game_keyboard.h"
#include "typing.h"
#include "tall3d.h"
#include "tall3d_engine.h"
#include "touch.h"
#include "vk_present.h"
#include "cores.h"
#include "wifi_port.h"

/* The DS's frame: 263 lines of 2130 bus cycles at 33,513,982 Hz, about 59.8261 a second. */
#define DS_CYCLES_PER_FRAME (263.0 * 2130.0)
#define DS_BUS_HZ 33513982.0

#define PATH_SIZE 1024

/* Options -------------------------------------------------------------------------------- */

static struct {
    char data[PATH_SIZE], rom[PATH_SIZE], save[PATH_SIZE], modules_dir[PATH_SIZE], manifest[PATH_SIZE];
    const char *rtc, *input;
    int read_only, scale, unpaced, linear, overlay, tall, full_touch;
    int sound;           /* the game's sound through the audio device */
    int resolution;      /* the 3D scene at this many times the DS's size (1 to 3) */
    int resetti;         /* Mr. Resetti comes after a play without a save, as on a DS */
    int autosave;        /* the port saves the town by itself (autosave.c) */
    int vulkan;          /* the window drawn through Vulkan, the scene at its size (vk_present.h) */
    int link;            /* the wireless on, and carried to a room's other consoles (netlink.h) */
    char link_server[128], link_room[40];
    int link_port;
    int wifi;            /* Nintendo Wi-Fi Connection through this machine's network (wifi_port.h) */
    char wifi_dns[64];   /* the name server the console asks: how a private server is reached */
    char wifi_mac[16];   /* the console's address for it, 12 hex digits, made once */
    int no_above;        /* the game's own choice of what to draw above its screen (--no-above) */
    const char *hash;    /* a file for the frames' hashes, as RUN_HASH (--hash) */
    int width, height;   /* the window's starting size (--size), else by --scale */
    uint32_t window_shot; /* the frame whose window is saved as a PNG (--window-shot) */
    uint32_t window_shots; /* and how many frames from it on (--window-shots; 1 without) */
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
            "  --resolution N  the 3D scene drawn at N times the DS's size, 1 to 3 (\"resolution=N\" in\n"
            "                  DATA/settings.txt keeps it); each step up costs several times the drawing\n"
            "  --link-host     play with others: open a room on this machine (port 28920, or\n"
            "                  --link-port N) for consoles on the same network to join\n"
            "  --link-join HOST[:PORT]  join a room at a relay (acww-relay, or a machine that\n"
            "                  opened one); --link-room NAME says which room (default \"acww\")\n"
            "                  (\"link=1\", \"link_server=\" and \"link_room=\" in DATA/settings.txt keep it)\n"
            "  --wifi, --no-wifi  Nintendo Wi-Fi Connection through this machine's network\n"
            "  --wifi-dns ADDR  with it: the name server the console asks (a private server's DNS)\n"
            "                  (\"wifi=1\" and \"wifi_dns=\" in DATA/settings.txt keep them)\n"
            "  --vulkan, --no-vulkan  the window drawn through Vulkan, the 3D scene at the window's\n"
            "                  own size and multisampled (on where the device can; \"vulkan=0\" in\n"
            "                  DATA/settings.txt keeps it)\n"
            "  --type-keyboard, --no-type-keyboard  type on the game's soft keyboard with this\n"
            "                  machine's (on where that is a keyboard on the screen;\n"
            "                  \"type_keyboard=\" in DATA/settings.txt keeps it)\n"
            "  --type FRAME:TEXT  for a check: TEXT as if typed at that frame (\\b: backspace, \\n: Enter)\n"
            "  --fps60, --no-fps60  through Vulkan: scenes made between the game's twenty a\n"
            "                  second, for sixty pictures a second at the game's own\n"
            "                  speed (on; \"fps60=0\" in DATA/settings.txt turns it off)\n"
            "  --no-sound      without sound (\"sound=0\" in DATA/settings.txt keeps it)\n"
            "  --audio-wav FILE  also write the mixer's sound to a new WAV file (the decomp's\n"
            "                  RUN_AUDIO_WAV). Its header says 32768 Hz; the frames are the DS's\n"
            "                  32728.5 a second\n"
            "  --touch         draw the touch buttons even without a touch screen\n"
            "  --autosave, --no-autosave  whether the port saves the town by itself, every minute\n"
            "                  the game would let you save, to SAVE.auto beside the save (default:\n"
            "                  it does; \"autosave=0\" in DATA/settings.txt keeps it off)\n"
            "  --load-autosave go on from SAVE.auto: it becomes the save (the old one is kept as\n"
            "                  SAVE.before-auto)\n"
            "  --resetti, --no-resetti  whether Mr. Resetti comes after a play that ended without a\n"
            "                  save (default: he does not; \"resetti=1\" in DATA/settings.txt keeps it)\n"
            "  --full-touch    start without the touch buttons (the stylus alone); the mark at the\n"
            "                  corner (the settings screen on Android) turns them on and off, and\n"
            "                  it is remembered\n"
            "  --size WxH      the window's starting size in pixels\n"
            "  --touch-script FILE  play fingers from a file (\"FRAME down|move|up FINGER X Y\")\n"
            "  --window-shot N save the window as it is drawn at frame N as a PNG in DATA\n"
            "  --window-shots COUNT  with --window-shot: that many frames from N on\n"
            "  --two-screens   the DS's two pictures as they are, without the 3D scene continued\n"
            "                  onto the upper screen\n"
            "  --no-above      with the tall view, draw only the objects the game itself would\n"
            "  --scene-dump    with --shot: the frame's 3D scene as scene-N.bin in DATA, for the\n"
            "                  renderer's tool (without it, at a larger size, tiles-N.bin: the 3D\n"
            "                  picture as the software draws it)\n"
            "  --hash FILE     write each frame's hashes there (the decomp's RUN_HASH), to compare runs\n"
            "  --picture-hash FILE  write a hash of each picture as the port shows it (the tall\n"
            "                  view and the larger scene included)\n"
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
/* The picture is PICTURE_W x PICTURE_H pixels: the DS's two screens at `picture_scale` times
 * their size (the 3D scene drawn that large, tall3d.h; 1 is the DS's own size). */
static int picture_scale = 1;
#define PICTURE_W (FRONT_SCREEN_W * picture_scale)
#define PICTURE_H (2 * FRONT_SCREEN_H * picture_scale)
static uint32_t shown[FRONT_SCREEN_W * TALL3D_SCALE_MAX * 2 * FRONT_SCREEN_H * TALL3D_SCALE_MAX];
static uint32_t shown_frame, drawn_frame;
static SDL_mutex *shown_lock;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *screens;
static Layout layout;

static uint32_t keys_held_keyboard, keys_held_pad, keys_taps;
static int pen_down, pen_x, pen_y, pen_tap, pen_tap_x, pen_tap_y;
static int mouse_pen, fast_keyboard, quit_asked;
static void picture_setup(void);
void gpu2d_main_probe_counts(unsigned long *lines, unsigned long *kept, unsigned long *wrong); /* gpu2d_main.c */
static int scene_dump;
static FILE *picture_hashes;
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

/* The port's own save (autosave.c, docs/autosave.md) ---------------------------------------- */

/* The town is saved beside the game's save file, as SAVE.auto: a whole image of the save
 * memory, with the town as it was at a moment the game would have let the player save. The
 * game's own file is never touched by it. At the next start the player is asked which to go
 * on from (LaunchActivity on Android; --load-autosave elsewhere), and the game's own save,
 * whenever it is made, takes the autosave away: it is the newer of the two. */
#define AUTOSAVE_EVERY (60 * 60) /* frames between saves: a minute */
#define AUTOSAVE_QUIET 30        /* frames the moment has to have lasted */

static uint32_t autosave_quiet_frames, autosave_frame;
static int autosave_taken;
static unsigned long saves_asked; /* by the player (save_now), and what the last one came to */
static int saves_said = -1;

static void autosave_path(char *out, size_t size)
{
    snprintf(out, size, "%s.auto", opt.save);
}

static int autosave_take(const char *why)
{
    static uint8_t card[AUTOSAVE_CARD_SIZE];
    char path[PATH_SIZE + 16], tmp[PATH_SIZE + 24];
    uint64_t writes;
    const uint8_t *image = arm7_backup(&writes);
    if (image == NULL || ARM7_BACKUP_SIZE != AUTOSAVE_CARD_SIZE) {
        return 0;
    }
    memcpy(card, image, AUTOSAVE_CARD_SIZE);
    if (autosave_snapshot(card) != 0) {
        return 0;
    }
    autosave_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (f == NULL || fwrite(card, 1, AUTOSAVE_CARD_SIZE, f) != AUTOSAVE_CARD_SIZE || fclose(f) != 0 || rename(tmp, path) != 0) {
        SDL_Log("play: cannot write %s", path);
        return 0;
    }
    autosave_frame = frame_now;
    autosave_taken = 1;
    SDL_Log("play: frame %u: autosave %s (%s)", frame_now, path, why);
    return 1;
}

/* Once a frame. */
static void autosave_frame_step(uint32_t frame)
{
    if (!opt.autosave || opt.read_only) {
        return;
    }
    const char *why;
    static const char *why_before;
    autosave_quiet_frames = autosave_quiet(&why) ? autosave_quiet_frames + 1 : 0;
    if (why != why_before && getenv("PORT_AUTOSAVE_TRACE") != NULL) {
        SDL_Log("play: frame %u: autosave: %s", frame, *why ? why : "a quiet moment");
    }
    why_before = why;
    if (autosave_quiet_frames >= AUTOSAVE_QUIET && (!autosave_taken || frame - autosave_frame >= AUTOSAVE_EVERY)) {
        autosave_take("a quiet moment");
    }
}

/* The app is about to be out of sight, where the system may end it: a save now, if the
 * moment allows; the last one stands otherwise. */
static void autosave_leaving(void)
{
    if (opt.autosave && !opt.read_only && autosave_quiet_frames >= AUTOSAVE_QUIET) {
        autosave_take("the app went to the background");
    }
}

/* The player asks for a save now (the mark at the window's top, or F5): the town as it is,
 * written as the autosave is, if this is a moment the game would let the player save in; if
 * not, the player is told what stands in the way. The numbers are MainActivity.saveSaid's. */
enum { SAVE_SAID_DONE = 0, SAVE_SAID_FAILED = 100, SAVE_SAID_READ_ONLY = 101 };

#ifdef __ANDROID__
static void activity_tell(const char *method, int number);
#endif

static void save_now(void)
{
    const char *why = "";
    int said = opt.read_only ? SAVE_SAID_READ_ONLY : autosave_reason(&why);
    if (said == AUTOSAVE_CAN) {
        said = autosave_take("the player asked") ? SAVE_SAID_DONE : SAVE_SAID_FAILED;
    } else {
        SDL_Log("play: frame %u: no save now: %s", frame_now, said == SAVE_SAID_READ_ONLY ? "nothing is written (--read-only)" : why);
    }
    saves_asked++;
    saves_said = said;
#ifdef __ANDROID__
    activity_tell("saveSaid", said);
#endif
}

/* The game wrote its own save: that is the newer one. */
static void autosave_game_saved(void)
{
    char path[PATH_SIZE + 16];
    autosave_path(path, sizeof(path));
    if (!opt.read_only && remove(path) == 0) {
        SDL_Log("play: the game saved: the autosave is taken away");
    }
    autosave_frame = frame_now;
    autosave_taken = 1; /* a minute until the next */
}

/* Wi-Fi Connection (wifi_port.h, docs/multiplayer.md) -------------------------------------- */

static void settings_save(void);
static int wifi_active; /* Wi-Fi Connection is on in this run */

/* The console's own things for it, kept in DATA: its address (made once, in the settings) and
 * its connection settings, wifi.bin, which the game's library writes its user ID into. A
 * server knows a console by the two: both stay with the save. */
static uint8_t wifi_kept[WIFI_PORT_SETTINGS_SIZE];
static int wifi_kept_valid;

static void wifi_path(char *out, size_t size)
{
    snprintf(out, size, "%s/wifi.bin", opt.data);
}

static void wifi_start(void)
{
    uint8_t mac[6] = {0x00, 0x09, 0xbf, 0, 0, 0}; /* Nintendo's block, as a DS's is */
    unsigned a = 0, b = 0, c = 0;
    char path[PATH_SIZE + 16];
    if (strlen(opt.wifi_mac) != 12 || sscanf(opt.wifi_mac + 6, "%2x%2x%2x", &a, &b, &c) != 3) {
        /* this console's address, once: from the clock's ticks and the machine's counter */
        uint64_t seed = SDL_GetPerformanceCounter() ^ ((uint64_t) time(NULL) << 20) ^ (uint64_t) (uintptr_t) &seed;
        seed ^= seed >> 29;
        seed *= 0x9e3779b97f4a7c15ull;
        seed ^= seed >> 32;
        a = (unsigned) (seed & 255), b = (unsigned) ((seed >> 8) & 255), c = (unsigned) ((seed >> 16) & 255);
        snprintf(opt.wifi_mac, sizeof(opt.wifi_mac), "0009bf%02x%02x%02x", a, b, c);
        settings_save();
    }
    mac[3] = (uint8_t) a, mac[4] = (uint8_t) b, mac[5] = (uint8_t) c;
    wifi_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    wifi_kept_valid = f != NULL && fread(wifi_kept, 1, sizeof(wifi_kept), f) == sizeof(wifi_kept);
    if (f != NULL) {
        fclose(f);
    }
    wifi_port_enable(opt.wifi_dns, mac, wifi_kept_valid ? wifi_kept : NULL, sizeof(wifi_kept));
    SDL_Log("play: Wi-Fi Connection: the console %s, name server %s%s", opt.wifi_mac, opt.wifi_dns[0] ? opt.wifi_dns : "this machine's",
            wifi_kept_valid ? ", its connection settings from wifi.bin" : "");
}

/* The connection settings written out when the game changed them. */
static void wifi_keep(void)
{
    const uint8_t *now = wifi_active ? wifi_port_settings() : NULL;
    char path[PATH_SIZE + 16], tmp[PATH_SIZE + 24];
    if (now == NULL || opt.read_only || (wifi_kept_valid && memcmp(now, wifi_kept, sizeof(wifi_kept)) == 0)) {
        return;
    }
    memcpy(wifi_kept, now, sizeof(wifi_kept));
    wifi_kept_valid = 1;
    wifi_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (f != NULL && fwrite(wifi_kept, 1, sizeof(wifi_kept), f) == sizeof(wifi_kept) && fclose(f) == 0 && rename(tmp, path) == 0) {
        SDL_Log("play: frame %u: the console's connection settings kept (%s)", frame_now, path);
    }
}

/* The console's reset (arm7_port.c), which the game does to start its Wi-Fi Connection
 * settings and to come back from them. */
void arm7_port_on_reset(void (*listener)(void));
unsigned arm7_port_resets(void);

static void console_reset(void)
{
    SDL_Log("play: frame %u: the console reset itself (the game's Wi-Fi settings start and end so)", frame_now);
}

/* At the start: with `load`, the autosave becomes the save the game loads (the save it
 * replaces is kept as SAVE.before-auto). */
static void autosave_at_start(int load)
{
    char path[PATH_SIZE + 16], kept[PATH_SIZE + 24];
    autosave_path(path, sizeof(path));
    if (!file_exists(path)) {
        return;
    }
    if (!load || opt.read_only) {
        SDL_Log("play: there is an autosave, %s; --load-autosave goes on from it", path);
        return;
    }
    snprintf(kept, sizeof(kept), "%s.before-auto", opt.save);
    if (file_exists(opt.save)) {
        remove(kept);
        rename(opt.save, kept);
    }
    if (rename(path, opt.save) == 0) {
        SDL_Log("play: going on from the autosave (the save before it: %s)", kept);
    }
}

/* Settings ---------------------------------------------------------------------------------- */

/* What the player chose while playing, kept in DATA/settings.txt as "name=value" lines. */
static void settings_path(char *out, size_t size)
{
    snprintf(out, size, "%s/settings.txt", opt.data);
}

/* The tall view as the settings have it: it takes effect when the game starts (the second 3D
 * engine cannot be started later), so it is kept apart from what this run uses (opt.tall). */
static int tall_setting = 1;
/* the 3D scene's size, likewise taking effect at the next start */
static int resolution_setting = 1;
/* the Vulkan renderer, likewise from the next start (the engines are set up for it then):
   on unless turned off; a device without it is found out at the start (open_window) */
static int vulkan_setting = 1;
/* the scene's picture through Vulkan against the window's pixels, in percent: at once */
static int vulkan_scale_setting = 100;
/* the threads kept to a phone's fast cores (cores.h): from the next start */
static int fast_cores_setting = 1;
/* sixty frames: scenes made between the game's twenty a second (scene_tween.h); at once */
static int fps60_setting = 1;
/* typing on the game's keyboard with the device's own (docs/typing.md): on where the device's
   keyboard is one on the screen, at once */
#ifdef __ANDROID__
static int type_setting = 1;
#else
static int type_setting = 0;
#endif
/* (the typing itself: see update_layout) */
static struct {
    int open;            /* the game's keyboard is up and the device's is asked for text */
    int phase, frames;   /* 0 nothing, 1 the pen on the key, 2 the pen off it */
    int tries, took;
    int away;            /* frames since the game's keyboard was last seen */
    int anew;            /* the session begins anew before the next key (the player touched the game's screen) */
    int covered;         /* the pixels of the window's height the device's keyboard covers */
    int confirm;         /* Enter was pressed where the text is one line: the screen's own button is to be touched */
    int confirm_x, confirm_y;
    int enter_keys, enter_texts; /* Enter since the last frame: as a key, and as the end of a line in text */
    char after[256];             /* what was typed after Enter, before the button was touched ("\b": a backspace) */
    size_t after_n;
    uint32_t looked;     /* the frame that was last asked */
    unsigned long keys, refused, sessions, confirms;
} typer;
static char *type_script;     /* --type: the text, and the frame it is typed at */
static uint32_t type_script_frame;

static unsigned long tween_scenes, tween_moved, tween_polygons; /* made, and how much of them moved */
static unsigned long scene_arrivals; /* the scenes the game has drawn */
static unsigned long scene_held;     /* frames that showed a scene again for want of the next */
static unsigned long scene_every[9]; /* how many came 1, 2 ... 7 and 8 or more frames after the one before */
static int tween_check;                           /* PORT_TWEEN_CHECK: scene_between_same for each pair */
static unsigned long tween_checked, tween_wrong;
static char cores_note[96];
/* "arrange=1": the settings screen asks for the arranging of the buttons (settings_apply) */
static int arrange_asked;

/* a gamepad's buttons (see pad_keys) */
static int pad_settings_line(const char *line);
static void pad_map_defaults(void);
static int pad_hide_setting = 1; /* the buttons on the screen are not shown while a gamepad is played with */
static int pad_used;             /* a gamepad's button has been pressed: one is being played with */

static void settings_load(void)
{
    char path[PATH_SIZE + 16], line[128];
    settings_path(path, sizeof(path));
    pad_map_defaults(); /* (a line that is gone leaves its button as it is without one) */
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        int v;
        if (sscanf(line, "full_touch=%d", &v) == 1) {
            opt.full_touch = v != 0;
        } else if (pad_settings_line(line)) {
            /* (a gamepad's buttons) */
        } else if (sscanf(line, "pad_hide=%d", &v) == 1) {
            pad_hide_setting = v != 0;
        } else if (sscanf(line, "type_keyboard=%d", &v) == 1) {
            type_setting = v != 0;
        } else if (sscanf(line, "resetti=%d", &v) == 1) {
            opt.resetti = v != 0;
        } else if (sscanf(line, "autosave=%d", &v) == 1) {
            opt.autosave = v != 0;
        } else if (sscanf(line, "vulkan=%d", &v) == 1) {
            vulkan_setting = v != 0;
        } else if (sscanf(line, "link=%d", &v) == 1) {
            opt.link = v != 0;
        } else if (sscanf(line, "wifi=%d", &v) == 1) {
            opt.wifi = v != 0;
        } else if (strncmp(line, "wifi_dns=", 9) == 0) {
            snprintf(opt.wifi_dns, sizeof(opt.wifi_dns), "%.63s", line + 9);
            opt.wifi_dns[strcspn(opt.wifi_dns, "\r\n")] = 0;
        } else if (strncmp(line, "wifi_mac=", 9) == 0) {
            snprintf(opt.wifi_mac, sizeof(opt.wifi_mac), "%.12s", line + 9);
            opt.wifi_mac[strcspn(opt.wifi_mac, "\r\n")] = 0;
        } else if (strncmp(line, "link_server=", 12) == 0) {
            snprintf(opt.link_server, sizeof(opt.link_server), "%.127s", line + 12);
            opt.link_server[strcspn(opt.link_server, "\r\n")] = 0;
        } else if (strncmp(line, "link_room=", 10) == 0) {
            snprintf(opt.link_room, sizeof(opt.link_room), "%.39s", line + 10);
            opt.link_room[strcspn(opt.link_room, "\r\n")] = 0;
        } else if (sscanf(line, "tall=%d", &v) == 1) {
            tall_setting = v != 0;
        } else if (sscanf(line, "vulkan_scale=%d", &v) == 1) {
            vulkan_scale_setting = v < 25 ? 25 : v > 100 ? 100 : v;
        } else if (sscanf(line, "fps60=%d", &v) == 1) {
            fps60_setting = v != 0;
        } else if (sscanf(line, "fast_cores=%d", &v) == 1) {
            fast_cores_setting = v != 0;
        } else if (sscanf(line, "sound=%d", &v) == 1) {
            opt.sound = v != 0;
        } else if (sscanf(line, "resolution=%d", &v) == 1) {
            resolution_setting = v < 1 ? 1 : v > TALL3D_SCALE_MAX ? TALL3D_SCALE_MAX : v;
        } else if (sscanf(line, "arrange=%d", &v) == 1) {
            arrange_asked = v != 0;
        } else {
            touch_settings_line(line);
        }
    }
    fclose(f);
}

/* The settings' file is the settings screen's as much as the game's (on Android,
 * SettingsActivity writes it while the game waits behind it). So the game writes only the
 * lines that are its own to change, and leaves every other line as it finds it: what it has
 * in memory of the others may be older than the file.
 *
 * Its own: full_touch (the mark on the screen), the buttons' arrangement, the console's
 * Wi-Fi address, and "vulkan" when it turns that off itself (vulkan_off says why, for the
 * settings screen). The settings screen's request to arrange the buttons ("arrange") is
 * taken out, having been taken. */
static const char *vulkan_off_reason; /* set when the game turns the setting off */

static int settings_own_line(const char *line)
{
    return strncmp(line, "full_touch=", 11) == 0 || strncmp(line, "wifi_mac=", 9) == 0 || strncmp(line, "arrange=", 8) == 0 ||
           strncmp(line, "button.", 7) == 0 ||
           (vulkan_off_reason != NULL && (strncmp(line, "vulkan=", 7) == 0 || strncmp(line, "vulkan_off=", 11) == 0));
}

static void settings_save(void)
{
    char path[PATH_SIZE + 16], tmp[PATH_SIZE + 24], line[256];
    if (opt.read_only) {
        return; /* (--read-only: nothing of DATA's is written) */
    }
    settings_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (f == NULL) {
        return;
    }
    FILE *old = fopen(path, "r");
    if (old != NULL) {
        while (fgets(line, sizeof(line), old) != NULL) {
            if (!settings_own_line(line) && line[strspn(line, " \t\r\n")] != 0) {
                fputs(line, f);
                if (line[strlen(line) - 1] != '\n') {
                    fputc('\n', f);
                }
            }
        }
        fclose(old);
    }
    fprintf(f, "full_touch=%d\n", opt.full_touch);
    if (opt.wifi_mac[0]) {
        fprintf(f, "wifi_mac=%s\n", opt.wifi_mac);
    }
    if (vulkan_off_reason != NULL) {
        fprintf(f, "vulkan=0\nvulkan_off=%s\n", vulkan_off_reason);
    }
    touch_settings_write(f);
    if (fclose(f) == 0) {
        rename(tmp, path); /* whole, or the old one stays */
    }
}

/* The window through Vulkan (vk_present.h, docs/renderer.md) --------------------------------- */

static VkPresenter *presenter;
static VkApi vk_api;
static VkInstance vk_instance;
static SDL_mutex *vk_lock; /* everything of Vulkan's once frames are drawn (draw_vulkan) */
static int vk_headless; /* no window to show on (the checks, under SDL's dummy driver): a surface of no display */
static PFN_vkGetInstanceProcAddr vk_get;
static int vulkan_explicit;     /* asked for on the command line (not only on by default) */
static int vulkan_lost;         /* the surface was let go of or failed: a new one before the next frame */
static int vulkan_failures;     /* new surfaces that did not work, in a row */
static Uint32 vulkan_retry_tick;
static unsigned vulkan_frames;  /* shown through Vulkan in this run */
static char vulkan_note[200];   /* why the window is not drawn through Vulkan, for the status */
static uint32_t vulkan_lose_frame; /* --vulkan-lose N: the app "leaves the screen" at frame N and returns (a check) */
static int app_away;               /* the app is leaving, or has left, the screen */
static unsigned vulkan_renewals, vulkan_renew_failures; /* new surfaces made, and not made, in this run */
static char vulkan_last_error[96];

/* The size of what is drawn on, in pixels. */
static void output_size(int *w, int *h)
{
    if (presenter == NULL) {
        SDL_GetRendererOutputSize(renderer, w, h);
    } else if (vk_headless) {
        SDL_GetWindowSize(window, w, h);
    } else {
        SDL_Vulkan_GetDrawableSize(window, w, h);
    }
}

/* Whether the window has something under it to make a surface on: on Android the system
 * takes the native window away while another screen is in front. */
static int window_ready(void)
{
#ifdef __ANDROID__
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    return SDL_GetWindowWMInfo(window, &info) && info.info.android.window != NULL;
#else
    return 1;
#endif
}

static VkSurfaceKHR vulkan_surface(PFN_vkGetInstanceProcAddr get)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const char *broken = getenv("PORT_VULKAN_BREAK"); /* a check: no new surface after the first */
    if (broken != NULL && *broken && (presenter != NULL || (*broken == '2' && vulkan_frames != 0))) {
        return VK_NULL_HANDLE; /* ("2": nor when everything is made anew) */
    }
    if (!vk_headless) {
        return SDL_Vulkan_CreateSurface(window, vk_instance, &surface) ? surface : VK_NULL_HANDLE;
    }
    PFN_vkCreateHeadlessSurfaceEXT create = (PFN_vkCreateHeadlessSurfaceEXT) get(vk_instance, "vkCreateHeadlessSurfaceEXT");
    VkHeadlessSurfaceCreateInfoEXT info = {.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
    return create != NULL && create(vk_instance, &info, NULL, &surface) == VK_SUCCESS ? surface : VK_NULL_HANDLE;
}

/* The instance, the surface and the presenter for the window that is open. 0, or -1 with a
 * word in the log on what there was not. */
static int vulkan_open(int headless)
{
    PFN_vkGetInstanceProcAddr get = NULL;
    const char *names[16];
    unsigned count = 16;
    vk_headless = headless;
    vk_instance = VK_NULL_HANDLE;
    if (headless) {
        void *library = SDL_LoadObject("libvulkan.so.1");
        get = library != NULL ? (PFN_vkGetInstanceProcAddr) SDL_LoadFunction(library, "vkGetInstanceProcAddr") : NULL;
        names[0] = VK_KHR_SURFACE_EXTENSION_NAME;
        names[1] = VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME;
        count = 2;
    } else {
        get = (PFN_vkGetInstanceProcAddr) SDL_Vulkan_GetVkGetInstanceProcAddr();
        if (!SDL_Vulkan_GetInstanceExtensions(window, &count, names)) {
            SDL_Log("play: vulkan: no surface extensions (%s)", SDL_GetError());
            snprintf(vulkan_note, sizeof(vulkan_note), "no Vulkan surface extensions");
            return -1;
        }
    }
    if (get == NULL || vk_api_load_global(&vk_api, get) != 0) {
        SDL_Log("play: vulkan: no Vulkan library");
        snprintf(vulkan_note, sizeof(vulkan_note), "no Vulkan library");
        return -1;
    }
    vk_get = get;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "acww-kor-port",
                             .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pApplicationInfo = &app,
                                 .enabledExtensionCount = count,
                                 .ppEnabledExtensionNames = names};
    if (vk_api.CreateInstance(&info, NULL, &vk_instance) != VK_SUCCESS) {
        SDL_Log("play: vulkan: no instance");
        snprintf(vulkan_note, sizeof(vulkan_note), "no Vulkan instance");
        vk_instance = VK_NULL_HANDLE;
        return -1;
    }
    if (vk_api_load(&vk_api, get, vk_instance) != 0 || vk_api_load_wsi(&vk_api, get, vk_instance) != 0) {
        SDL_Log("play: vulkan: the library lacks functions");
        snprintf(vulkan_note, sizeof(vulkan_note), "the Vulkan library lacks functions");
        return -1;
    }
    VkSurfaceKHR surface = vulkan_surface(get);
    if (surface == VK_NULL_HANDLE) {
        SDL_Log("play: vulkan: no surface (%s)", SDL_GetError());
        snprintf(vulkan_note, sizeof(vulkan_note), "no Vulkan surface");
        return -1;
    }
    int w, h;
    if (headless) {
        SDL_GetWindowSize(window, &w, &h);
    } else {
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
    }
    const char *why = NULL;
    presenter = vk_present_create(&vk_api, vk_instance, surface, w, h, 4, &why);
    if (presenter == NULL) {
        vk_api.DestroySurfaceKHR(vk_instance, surface, NULL);
        SDL_Log("play: vulkan: %s", why);
        snprintf(vulkan_note, sizeof(vulkan_note), "Vulkan: %s", why != NULL ? why : "the device cannot");
        return -1;
    }
    SDL_Log("play: the window through Vulkan on %s%s", vk_present_device(presenter), headless ? " (no display)" : "");
    if (vk_lock == NULL) {
        vk_lock = SDL_CreateMutex();
    }
    /* the game's own 3D picture is not drawn while the GPU's is the one seen, unless this
       run records the game's frames (hashes, shots) */
    tall3d_lean(!scene_dump && opt.shot_count == 0 && picture_hashes == NULL && (opt.hash == NULL || !*opt.hash) &&
                getenv("PORT_VULKAN_FULL") == NULL);
    vulkan_note[0] = 0;
    vulkan_lost = 0;
    vulkan_failures = 0;
    return 0;
}

/* What vulkan_open made, or as much of it as there is. */
static void vulkan_close(void)
{
    tall3d_lean(0);
    if (presenter != NULL) {
        vk_present_destroy(presenter);
        presenter = NULL;
    }
    if (vk_instance != VK_NULL_HANDLE && vk_api.DestroyInstance != NULL) {
        vk_api.DestroyInstance(vk_instance, NULL);
    }
    vk_instance = VK_NULL_HANDLE;
}

/* A Vulkan start that never got as far as showing frames (a driver that crashes or hangs in
 * it takes the app with it, every time): DATA/vulkan.trying counts the starts that did not,
 * and after three in a row the next start turns the setting off. The file goes once frames are shown, and
 * when the app is left in the ordinary way. */
static void vulkan_marker_path(char *out, size_t size)
{
    snprintf(out, size, "%s/vulkan.trying", opt.data);
}

static int vulkan_marker_count(void)
{
    char path[PATH_SIZE + 24];
    int count = 0;
    vulkan_marker_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f != NULL) {
        if (fscanf(f, "%d", &count) != 1) {
            count = 1;
        }
        fclose(f);
    }
    return count;
}

static void vulkan_marker_set(int count)
{
    char path[PATH_SIZE + 24];
    vulkan_marker_path(path, sizeof(path));
    if (opt.read_only) {
        return;
    }
    if (count <= 0) {
        remove(path);
        return;
    }
    FILE *f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "%d\n", count);
        fclose(f);
    }
}

/* The surface is let go of: the app leaves the screen (Android takes the window under it),
 * or a frame failed on it. Nothing is drawn until vulkan_renew has made a new one. */
static void vulkan_release(const char *why)
{
    if (presenter != NULL && !vulkan_lost) {
        SDL_Log("play: frame %u: the Vulkan surface is let go of (%s)", frame_now, why);
        vk_present_surface_gone(presenter);
        vulkan_lost = 1;
        vulkan_retry_tick = 0;
    }
}

/* How the run is going, in DATA/status.txt for the settings screen to show: the speed over
 * the last seconds (pace) and the sound's device (sound_sdl.c). What a player can read out
 * when something looks or sounds wrong on a phone. */
static double status_fps, status_busy;
static char link_failure[160]; /* why no room was joined, when one was asked for */

static void status_write(void)
{
    char path[1200], sound[160];
    snprintf(path, sizeof(path), "%s/status.txt", opt.data);
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    port_sound_status(sound, sizeof(sound));
    fprintf(f, "%s, %d-bit\n", PORT_VERSION, (int) (sizeof(void *) * 8));
    if (status_fps > 0) {
        fprintf(f, "%.1f fps (DS %.1f), busy %.0f%%\n", status_fps, DS_BUS_HZ / DS_CYCLES_PER_FRAME, status_busy * 100.0);
    }
    fprintf(f, "sound: %s\n", opt.sound ? sound : "off");
    fprintf(f, "cores: %s\n", cores_note[0] ? cores_note : "as the system puts the threads");
    if (opt.link) {
        char link[320];
        netlink_status(link, sizeof(link));
        fprintf(f, "link: %s%s%s\n", link, link_failure[0] ? ": " : "", link_failure);
    }
    if (wifi_active) {
        char wifi[320];
        wifi_port_status(wifi, sizeof(wifi));
        fprintf(f, "wifi: %s\n", wifi);
    }
    fprintf(f, "picture: %dx, %s, %s%s\n", picture_scale, opt.tall ? "tall" : "two screens",
            presenter != NULL ? "Vulkan on " : "without Vulkan", presenter != NULL ? vk_present_device(presenter) : "");
    if (presenter == NULL && vulkan_note[0]) {
        fprintf(f, "vulkan: %s\n", vulkan_note);
    } else if (presenter != NULL) {
        fprintf(f, "vulkan: %u frames, %u new surfaces, %u failures%s%s\n", vulkan_frames, vulkan_renewals, vulkan_renew_failures,
                vulkan_last_error[0] ? ", last: " : "", vulkan_last_error);
        for (int i = 0; i < 4; i++) {
            if (pads[i] != NULL) {
                fprintf(f, "gamepad: %s%s\n", SDL_GameControllerName(pads[i]) != NULL ? SDL_GameControllerName(pads[i]) : "(no name)",
                        pad_used ? ", played with" : "");
            }
        }
        if (saves_asked) {
            fprintf(f, "saves asked for: %lu, the last: %d\n", saves_asked, saves_said);
        }
        fprintf(f, "typing: %s, %lu keys on the game's keyboard in %lu sessions, %lu refused, %lu times Enter for the screen's button\n",
                type_setting ? "on" : "off", typer.keys, typer.sessions, typer.refused, typer.confirms);
        fprintf(f, "sixty frames: %s, %lu scenes made between, %lu%% of their polygons moved\n", fps60_setting ? "on" : "off",
                tween_scenes, tween_polygons ? tween_moved * 100 / tween_polygons : 0);
        fprintf(f, "scenes: %lu; frames after the one before: 2: %lu, 3: %lu, 4: %lu, 5: %lu, more: %lu; frames that waited for one: %lu\n",
                scene_arrivals, scene_every[2], scene_every[3], scene_every[4], scene_every[5],
                scene_every[6] + scene_every[7] + scene_every[8], scene_held);
    }
    fclose(f);
}

/* The settings screen, where the platform has one: on Android, SettingsActivity, asked for
 * through MainActivity.openSettings. It writes the settings' file, which is read again when
 * the game comes back to the front (settings_apply). */
#ifdef __ANDROID__
/* A method of MainActivity's without arguments or result. */
static int activity_call(const char *method)
{
    int called = 0;
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = SDL_AndroidGetActivity();
    if (env != NULL && activity != NULL) {
        jclass cls = (*env)->GetObjectClass(env, activity);
        jmethodID id = (*env)->GetMethodID(env, cls, method, "()V");
        if (id != NULL) {
            (*env)->CallVoidMethod(env, activity, id);
            called = 1;
        } else {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, cls);
        (*env)->DeleteLocalRef(env, activity);
    }
    return called;
}
#endif

#ifdef __ANDROID__
/* A method of MainActivity's that takes a number. */
static void activity_tell(const char *method, int number)
{
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = SDL_AndroidGetActivity();
    if (env != NULL && activity != NULL) {
        jclass cls = (*env)->GetObjectClass(env, activity);
        jmethodID id = (*env)->GetMethodID(env, cls, method, "(I)V");
        if (id != NULL) {
            (*env)->CallVoidMethod(env, activity, id, (jint) number);
        } else {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, cls);
        (*env)->DeleteLocalRef(env, activity);
    }
}

/* A method of MainActivity's without arguments that gives a number (0 without the method). */
static int activity_number(const char *method)
{
    int number = 0;
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = SDL_AndroidGetActivity();
    if (env != NULL && activity != NULL) {
        jclass cls = (*env)->GetObjectClass(env, activity);
        jmethodID id = (*env)->GetMethodID(env, cls, method, "()I");
        if (id != NULL) {
            number = (int) (*env)->CallIntMethod(env, activity, id);
        } else {
            (*env)->ExceptionClear(env);
        }
        (*env)->DeleteLocalRef(env, cls);
        (*env)->DeleteLocalRef(env, activity);
    }
    return number;
}
#endif

static void open_settings(void)
{
    status_write();
#ifdef __ANDROID__
    activity_call("openSettings");
#endif
}

/* Android's Back: the platform asks whether to leave the game (MainActivity.confirmExit,
 * which sends SDL_QUIT on a yes). Elsewhere, where the key hardly exists, the buttons go off
 * or on as the mark does. */
static void toggle_buttons(void);

static void back_pressed(void)
{
#ifdef __ANDROID__
    if (activity_call("confirmExit")) {
        return;
    }
#endif
    toggle_buttons();
}

/* The settings screen's request to arrange the buttons, if there is one. */
static void arrange_take(void)
{
    if (arrange_asked) {
        /* asked for on the settings screen: the buttons shown, and their arranging open */
        arrange_asked = 0;
        opt.full_touch = 0;
        touch_arrange_open();
        settings_save(); /* without the request */
    }
}

static void settings_apply(void)
{
    settings_load();
    game_hooks_resetti(opt.resetti);
    game_keyboard_enable(type_setting);
    arrange_take();
}

/* The window -------------------------------------------------------------------------------- */

/* the picture enlarged by a whole number (enlarged) */
static SDL_Texture *sharp;
static int sharp_factor;

/* The renderer and the picture's texture. Also after the graphics device was lost (a phone
 * may take it while another screen is in front: SDL_RENDER_DEVICE_RESET), when everything
 * made with the old one is gone. */
static int open_renderer(void)
{
    if (sharp != NULL) {
        SDL_DestroyTexture(sharp);
        sharp = NULL;
    }
    if (screens != NULL) {
        SDL_DestroyTexture(screens);
        screens = NULL;
    }
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    screens = renderer != NULL ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING,
                                                   PICTURE_W, PICTURE_H)
                               : NULL;
    if (screens == NULL) {
        return -1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return 0;
}

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
    const int width = opt.width > 0 ? opt.width : FRONT_SCREEN_W * opt.scale;
    const int height = opt.height > 0 ? opt.height : 2 * FRONT_SCREEN_H * opt.scale;
    const char *driver = SDL_GetCurrentVideoDriver();
    const int no_display = driver != NULL && strcmp(driver, "dummy") == 0;
    if (opt.vulkan && no_display && !vulkan_explicit) {
        opt.vulkan = 0; /* the checks' windows of no display: through Vulkan only when asked */
        picture_setup();
    }
    if (opt.vulkan && !no_display && vulkan_marker_count() >= 3) {
        SDL_Log("play: Vulkan did not get through its start the last three times: off (\"vulkan=0\" in the settings)");
        snprintf(vulkan_note, sizeof(vulkan_note), "turned off: the last three starts with it did not get through");
        vulkan_setting = 0;
        vulkan_off_reason = "starts";
        settings_save();
        vulkan_off_reason = NULL;
        vulkan_marker_set(0);
        opt.vulkan = 0;
        picture_setup();
    }
    if (opt.vulkan) {
        /* through Vulkan: a window made for it (first: Android has one window, no more).
           Where the video driver has no Vulkan (SDL's dummy one, in the checks), a plain
           window and a surface without a display. Where the device cannot (no Vulkan, or
           not what the renderer needs of it), the window as ever and the engines set up for
           that: the setting stays, the device is asked again at the next start */
        if (!no_display) {
            vulkan_marker_set(vulkan_marker_count() + 1);
            window = SDL_CreateWindow("acww-kor-port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                      flags | SDL_WINDOW_VULKAN);
            if (window != NULL && vulkan_open(0) == 0) {
                return 0;
            }
            vulkan_close();
            if (window != NULL) {
                SDL_DestroyWindow(window);
            }
            vulkan_marker_set(0); /* it said no in good order */
        }
        window = SDL_CreateWindow("acww-kor-port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
        if (window != NULL && no_display && vulkan_open(1) == 0) {
            return 0;
        }
        vulkan_close();
        SDL_Log("play: no Vulkan on this device (%s): the window without it", vulkan_note);
        opt.vulkan = 0;
        picture_setup();
    } else {
        window = SDL_CreateWindow("acww-kor-port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    }
    if (window == NULL || open_renderer() != 0) {
        tell("acww-kor", SDL_GetError());
        return -1;
    }
    return 0;
}

/* Typing with the device's own keyboard --------------------------------------------------------
 *
 * While one of the game's screens with its soft keyboard is up (game_keyboard.h), the
 * device's keyboard is asked for text, and what it types is typed on the game's keyboard, a
 * key at a time: the pen is put on the game's space bar, and the game is given the character
 * wanted for the key's code (typing.h says which key is next). A key takes two turns of the
 * game, some eight a second.
 *
 * A keyboard on the screen covers the lower part of the window, where the DS's lower screen
 * is, and that screen is the one being typed on: so while it is covered, the lower screen is
 * shown in the part of the window that is left, at the top, and the upper screen is out of
 * sight above it. */
#ifdef __ANDROID__
static int activity_call(const char *method);
static int activity_number(const char *method);
#endif

/* A session of typing begins: the device's keyboard starts with no text, and nothing in the
 * game is that text's. */
static void typer_begin(void)
{
    static const TypingGame game = {game_keyboard_gives, game_keyboard_syllable};
    typing_start(&game);
    game_keyboard_let_go();
    typer.phase = typer.frames = typer.tries = typer.anew = typer.confirm = 0;
    typer.after_n = 0;
    typer.sessions++;
#ifdef __ANDROID__
    activity_call("restartTextInput"); /* (its text so far, and a syllable being typed, are let go of) */
#endif
}

/* How much of the window the device's keyboard covers now. */
static int typer_covered(int h)
{
    const char *test = getenv("PORT_TYPE_COVER"); /* (a check without a phone: the pixels covered) */
    int covered = 0;
    if (test != NULL) {
        covered = atoi(test);
    }
#ifdef __ANDROID__
    covered = activity_number("keyboardCover");
#endif
    return covered * 8 > h ? covered : 0; /* (less than an eighth: a bar of the system's, not a keyboard) */
}

static void typer_enter(void);
static void typer_text(const char *text);
static void typer_backspace(void);

/* Each frame, before the game is given its input: the pen is the typing's while a key is
 * being typed. */
static void typer_frame(uint32_t frame, int *down, int *x, int *y)
{
    int up = type_setting && game_keyboard_ready();
    if (up) {
        typer.away = 0;
    }
    if (up && !typer.open) {
        typer.open = 1;
        typer_begin();
        SDL_StartTextInput();
        SDL_Log("play: frame %u: the game's keyboard is up: typing with the device's", frame);
    } else if (!up && typer.open && ++typer.away > 45) {
        typer.open = 0;
        typer.covered = 0;
        SDL_StopTextInput();
        game_keyboard_let_go();
        keys_held_keyboard = 0;
        SDL_Log("play: frame %u: the game's keyboard is gone", frame);
    }
    /* Enter comes as a key from some of the device's keyboards and as the end of a line in
       text from others, and from those as a key as well (SDL makes one of it): it is one
       Enter all the same */
    if (typer.open && typer.enter_keys > typer.enter_texts) {
        typer_enter();
    }
    typer.enter_keys = typer.enter_texts = 0;
    if (!typer.open) {
        return;
    }
    if (frame - typer.looked >= 6) {
        int w, h;
        output_size(&w, &h);
        typer.looked = frame;
        typer.covered = typer_covered(h);
    }
    if (type_script != NULL && frame >= type_script_frame) {
        /* --type: "\b" in the text is the keyboard's backspace, "\n" its Enter */
        for (char *p = type_script; *p != 0;) {
            if (p[0] == '\\' && (p[1] == 'b' || p[1] == 'n')) {
                if (p[1] == 'b') {
                    typer_backspace();
                } else {
                    /* (as the key a keyboard gives, by the way of the keys) */
                    SDL_Event key = {0};
                    key.type = SDL_KEYDOWN;
                    key.key.state = SDL_PRESSED;
                    key.key.keysym.sym = SDLK_RETURN;
                    key.key.keysym.scancode = SDL_SCANCODE_RETURN;
                    SDL_PushEvent(&key);
                    key.type = SDL_KEYUP;
                    key.key.state = SDL_RELEASED;
                    SDL_PushEvent(&key);
                    type_script = p + 2; /* (the rest after the key has come) */
                    type_script_frame = frame + 2;
                    return;
                }
                p += 2;
                continue;
            }
            char *b = strchr(p + 1, '\\');
            char keep = b != NULL ? *b : 0;
            if (b != NULL) {
                *b = 0;
            }
            typer_text(p);
            p = b != NULL ? b : p + strlen(p);
            if (b != NULL) {
                *b = keep;
            }
        }
        type_script = NULL;
    }
    if (!up) {
        return; /* (the game is between two of its screens: nothing is typed meanwhile) */
    }
    if (typer.phase == 0) {
        if (game_keyboard_foreign() || typer.anew || (typing_long() && !typing_pending())) {
            typer_begin(); /* the game's text was changed by the game's own keys or the pen */
        }
        uint32_t key = *down ? 0 : typing_next(); /* (not while the player's own pen is down) */
        if (key == 0 && typer.confirm == 1 && !*down) {
            /* Enter, and everything typed is in the game: first a key that does nothing, which
               shows that the game is taking keys now (and turns it to the pen); then the
               screen's own button (below) */
            key = KEYBOARD_NOTHING;
            typer.confirm = 2;
        }
        if (key != 0) {
            game_keyboard_type(key);
            typer.phase = 1;
            typer.frames = 0;
        }
    }
    if (typer.phase == 1) {
        *down = 1;
        *x = KEYBOARD_TOUCH_X;
        *y = KEYBOARD_TOUCH_Y;
        typer.took = game_keyboard_taken();
        if (typer.took || ++typer.frames >= 12) {
            if (!typer.took) {
                game_keyboard_let_go();
            }
            typer.phase = 2;
            typer.frames = 0;
        }
    } else if (typer.phase == 3) {
        /* the screen's own button (Decide, or the arrow that sends), touched as a finger would */
        *down = 1;
        *x = typer.confirm_x;
        *y = typer.confirm_y;
        if (++typer.frames >= 5) {
            typer.phase = 4;
            typer.frames = 0;
        }
    } else if (typer.phase == 4) {
        *down = 0;
        if (++typer.frames >= 5) {
            /* what the screen does next is the game's: the typing begins anew, with what was
               typed meanwhile (a next line of chat; a name's screen is over) */
            char held[sizeof(typer.after) + 1];
            int chat = game_keyboard_screen() == KEYBOARD_SCREEN_CHAT;
            memcpy(held, typer.after, typer.after_n);
            held[typer.after_n] = 0;
            typer.confirms++;
            typer_begin();
            for (char *p = held; chat && *p != 0;) {
                if (*p == '\b') {
                    typer_backspace();
                    p++;
                } else {
                    char *e = p;
                    while (*e != 0 && *e != '\b' && *e != '\n') {
                        e++;
                    }
                    e += *e == '\n';
                    char keep = *e;
                    *e = 0;
                    typer_text(p);
                    *e = keep;
                    p = e;
                }
            }
        }
    } else if (typer.phase == 2) {
        *down = 0;
        /* (the game sees the pen gone only after some frames, and takes no key for a turn
           after one: with less than four frames here, a touch is not taken as a new one) */
        if (++typer.frames >= 5) {
            typer.phase = 0;
            if (!typer.took) {
                /* the game did not ask for the key: the first touch after its buttons were
                   used only turns it to the pen; after that, it is not taking keys now (a
                   question is up), and what is waiting is given up */
                if (++typer.tries >= 3) {
                    typer_begin();
                } else if (typer.confirm == 2) {
                    typer.confirm = 1; /* (the key that does nothing, again) */
                }
            } else if (game_keyboard_refused()) {
                typer.refused++; /* the text has no room for it: what was typed past it is let go of */
                typer_begin();
            } else if (typer.confirm == 2) {
                typer.confirm = 3; /* the game is taking keys: its button now */
                typer.phase = 3;
                typer.frames = 0;
                typer.tries = 0;
            } else {
                typing_took();
                typer.keys++;
                typer.tries = 0;
            }
        }
    }
}

/* What is typed after Enter, while the screen's button is yet to be touched, is typed after
 * it. */
static void typer_hold(const char *text)
{
    size_t n = strlen(text);
    if (typer.after_n + n <= sizeof(typer.after)) {
        memcpy(typer.after + typer.after_n, text, n);
        typer.after_n += n;
    }
}

static void typer_backspace(void)
{
    if (typer.confirm != 0) {
        typer_hold("\b");
    } else {
        typing_backspace();
    }
}

/* Enter on the device's keyboard. Where the text is one line (a name, a line of chat), it is
 * the screen's own button that ends the typing: Decide, or the arrow that sends the line;
 * touched once everything typed is in the game. Where the text has lines (a letter, the
 * board), it is the end of a line. */
static void typer_enter(void)
{
    int screen = game_keyboard_screen();
    if (typer.confirm != 0) {
        typer_hold("\n");
        return;
    }
    if (screen == KEYBOARD_SCREEN_ENTRY || screen == KEYBOARD_SCREEN_CHAT) {
        if (typer.confirm == 0) {
            typer.confirm = 1;
            typer.confirm_x = screen == KEYBOARD_SCREEN_ENTRY ? KEYBOARD_ENTRY_DECIDE_X : KEYBOARD_CHAT_SEND_X;
            typer.confirm_y = screen == KEYBOARD_SCREEN_ENTRY ? KEYBOARD_ENTRY_DECIDE_Y : KEYBOARD_CHAT_SEND_Y;
        }
    } else {
        typing_text("\n");
    }
}

/* Text from the device's keyboard. An end of a line in it is Enter (above): where the text is
 * one line, after the rest of the text. */
static void typer_text(const char *text)
{
    int screen = game_keyboard_screen();
    int line = screen == KEYBOARD_SCREEN_ENTRY || screen == KEYBOARD_SCREEN_CHAT;
    char *kept;
    size_t n = 0;
    int ends = 0;
    if (typer.confirm != 0) {
        for (const char *p = text; *p != 0; p++) {
            typer.enter_texts += *p == '\n';
        }
        typer_hold(text);
        return;
    }
    kept = malloc(strlen(text) + 1);
    if (kept == NULL) {
        return;
    }
    for (const char *p = text; *p != 0; p++) {
        if (*p == '\r') {
            continue;
        }
        if (*p == '\n') {
            ends++;
            if (line) {
                continue;
            }
        }
        kept[n++] = *p;
    }
    kept[n] = 0;
    if (n > 0) {
        typing_text(kept);
    }
    free(kept);
    if (ends > 0) {
        typer.enter_texts += ends;
        if (line) {
            typer_enter();
        }
    }
}

/* A key of a keyboard while typing: 1 when it is the typing's (and not a button of the DS). */
static int typer_key(const SDL_Keysym *key, int pressed)
{
    if (!typer.open) {
        return 0;
    }
    if (key->sym == SDLK_BACKSPACE) {
        if (pressed) {
            typer_backspace();
        }
        return 1;
    }
    if (key->sym == SDLK_RETURN || key->sym == SDLK_KP_ENTER) {
        if (pressed) {
            typer.enter_keys++; /* (looked at in typer_frame: it may be in the text as well) */
        }
        return 1;
    }
    /* the keys that give text come as text; the others (the arrows, Escape) are the DS's still */
    return key->sym >= 0x20 && key->sym < 0x7f;
}

static void update_layout(void)
{
    int w, h;
    output_size(&w, &h);
    int touch = opt.overlay || SDL_GetNumTouchDevices() > 0;
    if (typer.open && typer.covered > 0) {
        /* the device's keyboard is over the lower part of the window: the lower screen, which
           is typed on, in the part above it, and the upper screen out of sight above that */
        layout = layout_compute(w, h, touch, 1);
        layout.save = (SDL_Rect){0, 0, 0, 0}; /* (the lower screen is at the top now) */
        double room = h - typer.covered, scale = room / layout.bottom.h;
        scale = scale > 1.0 ? 1.0 : scale;
        int bw = (int) (layout.bottom.w * scale), bh = (int) (layout.bottom.h * scale);
        int bx = (w - bw) / 2;
        layout.bottom = (SDL_Rect){bx, 0, bw, bh}; /* where the upper screen is otherwise: at the window's top */
        layout.top = (SDL_Rect){bx, -bh, bw, bh};
        return;
    }
    /* (played with a gamepad, the buttons on the screen are in the way: the pen alone then) */
    layout = layout_compute(w, h, touch, opt.full_touch || (pad_hide_setting && pad_used));
}

/* The picture enlarged for the window. A phone's screen is rarely a whole multiple of the
 * DS's, and neither way of scaling by a fraction looks right: the nearest pixel makes them
 * uneven, a linear filter blurs them. So, unless --smooth asks for the plain linear filter,
 * the picture is first enlarged by a whole number with its pixels kept square (into `sharp`),
 * and that is taken to the window's size with the linear filter, which then only softens the
 * pixels' edges. */

static SDL_Texture *enlarged(const SDL_Rect *to)
{
    int factor = (to->w + PICTURE_W - 1) / PICTURE_W, most = 4 / picture_scale;
    factor = factor < 1 ? 1 : factor > most ? (most < 1 ? 1 : most) : factor;
    if (opt.linear || to->w == PICTURE_W * factor || !SDL_RenderTargetSupported(renderer)) {
        return screens; /* linear was asked for, or the size is a whole multiple: as it is */
    }
    if (sharp == NULL || sharp_factor != factor) {
        if (sharp != NULL) {
            SDL_DestroyTexture(sharp);
        }
        sharp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, PICTURE_W * factor,
                                  PICTURE_H * factor);
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

/* --window-shot, --window-shots: this frame's window is saved */
static int window_shot_at(uint32_t frame)
{
    return opt.window_shot != 0 && frame >= opt.window_shot && frame - opt.window_shot < (opt.window_shots ? opt.window_shots : 1);
}

static void window_shot(void)
{
    int w, h;
    output_size(&w, &h);
    uint32_t *pixels = malloc((size_t) w * (size_t) h * 4);
    char path[PATH_SIZE + 64];
    snprintf(path, sizeof(path), "%s/window-%05u.png", opt.data, frame_now);
    if (pixels != NULL && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB888, pixels, w * 4) == 0) {
        png_write(path, pixels, w, h);
    }
    free(pixels);
}

/* What a frame through Vulkan is made of, as the drawing thread left it (on_frame_output):
 * the two screens, the 2D engine's three answers, and the scene of the 3D layer. */
#define DS_PIXELS (FRONT_SCREEN_W * FRONT_SCREEN_H)
static struct {
    uint32_t upper[DS_PIXELS], lower[DS_PIXELS], black[DS_PIXELS], white[DS_PIXELS], none[DS_PIXELS];
    int answers;      /* the three answers are there */
    int scene_shown;  /* the lower screen shows the 3D layer */
    int over_upper;
    uint32_t brightness;
} packet;
static Scene *scene_latest;  /* from the scene engine, not yet a frame's (scene_lock) */
static Scene *scene_current; /* the scene of the frame shown (shown_lock) */
/* sixty frames (scene_frame) */
static Scene *scene_last, *scene_from; /* the scene being gone to, and the one the way to it starts at */
static Scene *scene_wait;              /* the game's next scene, until the way to that one is done */
static uint32_t scene_usual = 3;       /* how many frames apart the game's scenes usually are */
static unsigned scene_idle;
static uint32_t scene_last_output, scene_outputs;
static uint32_t scene_steps = 1, scene_step = 2; /* the frames the way takes, and the next one's number */

static SDL_mutex *scene_lock;

/* A frame through Vulkan is drawn by a thread of its own, so that the emulation never waits
 * for the GPU (a frame's drawing waits for the one before it to be done, and for a picture
 * of the window to be free). The emulation's thread says what there is to draw (`job`: where
 * the screens are in the window, the buttons' triangles) and goes on; the drawing takes the
 * newest of everything when it comes to it, so a GPU that is slower than the game leaves
 * frames out and nothing queues up. Everything of Vulkan's is done under vk_lock, by this
 * thread for the frames and by the emulation's for the surface's coming and going.
 *
 * A frame is drawn as its picture is finished (on_frame_output, on the drawing thread): each
 * of the sixty a second once and in its turn. Were it drawn at the emulation's word, a
 * picture finished a moment late would be left out and the one before it shown twice, which
 * shows once the scene moves in every frame (scene_frame). The emulation's thread asks for
 * the drawing itself in fast forward (a frame in ten) and when no picture has come for two
 * frames. */
static struct {
    SDL_mutex *lock;
    SDL_cond *wake;
    SDL_Thread *thread;
    unsigned posted, taken;
    int by_output;           /* frames are drawn as their pictures are finished */
    int forced;              /* this one at the emulation's word all the same */
    unsigned output_posts;   /* how many were asked for so */
    uint32_t output_frame;   /* the last finished picture's frame */
    uint32_t capture_output; /* the capture is of this frame's picture, or of a later one */
    int w, h, top[4], bottom[4];
    int ui_count, scene_percent;
    uint32_t capture_frame; /* this frame's window is saved as a PNG; 0 for none */
    TouchVertex ui[32768];
} job;

/* On the drawing's thread, under vk_lock. */
static void draw_vulkan_frame(int w, int h, const int top[4], const int bottom[4], const TouchVertex *ui, int ui_count,
                              int scene_percent, uint32_t capture_frame)
{
    static uint32_t upper[DS_PIXELS], lower[DS_PIXELS], black[DS_PIXELS], white[DS_PIXELS], none[DS_PIXELS];
    PresentFrame f;
    memset(&f, 0, sizeof(f));
    SDL_LockMutex(shown_lock);
    memcpy(upper, packet.upper, sizeof(upper));
    memcpy(lower, packet.lower, sizeof(lower));
    if (packet.answers) {
        memcpy(black, packet.black, sizeof(black));
        memcpy(white, packet.white, sizeof(white));
        memcpy(none, packet.none, sizeof(none));
        f.black = black;
        f.white = white;
        f.none = none;
    }
    /* the scene is this function's until it is given back below */
    Scene *scene = scene_current;
    scene_current = NULL;
    f.scene = packet.scene_shown && packet.answers ? scene : NULL;
    f.scene_over_upper = packet.over_upper;
    f.brightness = packet.brightness;
    SDL_UnlockMutex(shown_lock);

    f.upper = upper;
    f.lower = lower;
    memcpy(f.top, top, sizeof(f.top));
    memcpy(f.bottom, bottom, sizeof(f.bottom));
    f.ui = (const PresentVertex *) ui;
    f.ui_count = ui_count;
    f.capture = capture_frame != 0;
    f.scene_percent = scene_percent;
    static int no_draw = -1; /* PORT_VULKAN_NO_DRAW: a measurement of everything but the GPU's part */
    if (no_draw < 0) {
        no_draw = getenv("PORT_VULKAN_NO_DRAW") != NULL;
    }
    int got = no_draw ? 0 : vk_present_frame(presenter, &f, w, h);
    if (got != 0) {
        /* the surface went (with the app's leaving the screen, or however the system says
           it): let go of, and a new one before the next frame (vulkan_renew) */
        vulkan_failures += got < 0;
        SDL_Log("play: a frame through Vulkan failed (%s)", vk_present_error(presenter));
        vulkan_release(got < 0 ? "a frame failed" : "it is gone");
    } else if (++vulkan_frames == 120) {
        vulkan_marker_set(0);
    }
    if (f.capture && got == 0) {
        int pw, ph, bgra;
        const uint8_t *pixels = vk_present_pixels(presenter, &pw, &ph, &bgra);
        uint32_t *rgb = pixels != NULL ? malloc((size_t) pw * (size_t) ph * 4) : NULL;
        if (rgb != NULL) {
            char path[PATH_SIZE + 64];
            for (int i = 0; i < pw * ph; i++) {
                const uint8_t *q = pixels + 4 * i;
                rgb[i] = bgra ? (uint32_t) q[2] << 16 | (uint32_t) q[1] << 8 | q[0] : (uint32_t) q[0] << 16 | (uint32_t) q[1] << 8 | q[2];
            }
            snprintf(path, sizeof(path), "%s/window-%05u.png", opt.data, capture_frame);
            png_write(path, rgb, pw, ph);
            free(rgb);
        }
    }
    SDL_LockMutex(shown_lock);
    if (scene_current == NULL) {
        scene_current = scene; /* still the scene of the frame shown */
    } else {
        scene_free(scene);
    }
    SDL_UnlockMutex(shown_lock);
}

static int vulkan_thread(void *arg)
{
    static TouchVertex ui[sizeof(job.ui) / sizeof(job.ui[0])];
    (void) arg;
    for (;;) {
        int w, h, top[4], bottom[4], ui_count, scene_percent;
        uint32_t capture_frame;
        SDL_LockMutex(job.lock);
        while (job.posted == job.taken) {
            SDL_CondWait(job.wake, job.lock);
        }
        job.taken = job.posted;
        w = job.w;
        h = job.h;
        memcpy(top, job.top, sizeof(top));
        memcpy(bottom, job.bottom, sizeof(bottom));
        ui_count = job.ui_count;
        scene_percent = job.scene_percent;
        memcpy(ui, job.ui, (size_t) ui_count * sizeof(ui[0]));
        capture_frame = job.capture_frame;
        if (job.by_output && !job.forced && job.output_frame < job.capture_output) {
            capture_frame = 0; /* the picture asked for is still to come */
        } else {
            job.capture_frame = 0;
        }
        job.forced = 0;
        SDL_UnlockMutex(job.lock);

        SDL_LockMutex(vk_lock);
        if (presenter != NULL && !vulkan_lost) {
            draw_vulkan_frame(w, h, top, bottom, ui, ui_count, scene_percent, capture_frame);
        }
        SDL_UnlockMutex(vk_lock);
        if (capture_frame != 0) {
            SDL_LockMutex(job.lock);
            SDL_CondBroadcast(job.wake); /* (the emulation's thread may be waiting for the picture) */
            SDL_UnlockMutex(job.lock);
        }
    }
    return 0;
}

/* On the emulation's thread: the frame shown is to be drawn. */
static void draw_vulkan(void)
{
    if (job.thread == NULL) {
        job.lock = SDL_CreateMutex();
        job.wake = SDL_CreateCond();
        job.thread = SDL_CreateThread(vulkan_thread, "vulkan", NULL);
    }
    update_layout();
    int w, h;
    output_size(&w, &h);
    SDL_LockMutex(job.lock);
    job.w = w;
    job.h = h;
    job.top[0] = layout.top.x, job.top[1] = layout.top.y, job.top[2] = layout.top.w, job.top[3] = layout.top.h;
    job.bottom[0] = layout.bottom.x, job.bottom[1] = layout.bottom.y, job.bottom[2] = layout.bottom.w, job.bottom[3] = layout.bottom.h;
    job.ui_count = 0;
    job.scene_percent = vulkan_scale_setting;
    if (layout.touch) {
        job.ui_count = touch_draw_list(&layout, keys_held_keyboard | keys_held_pad | touch_keys(), job.ui,
                                       (int) (sizeof(job.ui) / sizeof(job.ui[0])));
    }
    int capture = window_shot_at(frame_now);
    if (capture) {
        job.capture_frame = frame_now;
        job.capture_output = frame_now - 1; /* (the picture of the frame just run: they are counted from 0) */
    }
    /* who asks for the drawing: the finished pictures, or this thread */
    static unsigned posts_seen;
    static int quiet;
    quiet = job.output_posts == posts_seen ? quiet + 1 : 0;
    posts_seen = job.output_posts;
    job.by_output = !(fast_keyboard || opt.unpaced);
    if (!job.by_output || quiet >= 2 || (capture && job.output_frame + 1 >= frame_now)) {
        job.forced = 1; /* the newest picture, whichever it is (one to be saved may be out already) */
        job.posted++;
        SDL_CondBroadcast(job.wake);
    }
    /* a window's picture that was asked for is waited for (a check, or a screenshot): the
       run may end right after this frame */
    Uint32 since = SDL_GetTicks();
    while (capture && job.capture_frame != 0 && SDL_GetTicks() - since < 5000) {
        SDL_CondWaitTimeout(job.wake, job.lock, 100);
    }
    SDL_UnlockMutex(job.lock);
    if (capture) {
        /* (and its drawing, which follows the taking) */
        SDL_LockMutex(vk_lock);
        SDL_UnlockMutex(vk_lock);
    }
    drawn_frame = shown_frame;
}

/* Vulkan is given up in the middle of a run (new surfaces keep failing): the window as it is
 * without Vulkan, the engines set up for the software picture again (the tall view with it),
 * for the rest of this run. The setting stays: the next start tries Vulkan again. */
static void vulkan_give_up(void)
{
    SDL_Log("play: frame %u: Vulkan cannot draw on the window any more (%s): the software picture for the rest of this run",
            frame_now, vulkan_last_error[0] ? vulkan_last_error : "no new surface");
    snprintf(vulkan_note, sizeof(vulkan_note), "given up in this run after %u failures (%s)", vulkan_renew_failures,
             vulkan_last_error[0] ? vulkan_last_error : "no new surface");
    vulkan_close();
    vulkan_lost = 0;
    opt.vulkan = 0;
    picture_setup();
    Uint32 flags = SDL_GetWindowFlags(window) & (SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_FULLSCREEN_DESKTOP);
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    SDL_DestroyWindow(window);
    window = SDL_CreateWindow("acww-kor-port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (window == NULL || open_renderer() != 0) {
        SDL_Log("play: no window (%s)", SDL_GetError());
        quit_asked = 1;
    }
}

/* A new surface for the window, and the swapchain on it. Tried ten times a second while
 * there is none; after three failures everything of Vulkan's is made anew, and after six
 * more it is given up for this run. */
static void vulkan_renew(void)
{
    Uint32 now = SDL_GetTicks();
    if (vulkan_retry_tick != 0 && now - vulkan_retry_tick < 100) {
        return;
    }
    vulkan_retry_tick = now != 0 ? now : 1;
    if (!window_ready()) {
        return; /* the app is not on the screen */
    }
    if (vulkan_failures >= 10) {
        vulkan_give_up();
        return;
    }
    if (vulkan_failures == 3) {
        SDL_Log("play: frame %u: Vulkan from the start again", frame_now);
        vulkan_close();
        if (vulkan_open(vk_headless) != 0) {
            snprintf(vulkan_last_error, sizeof(vulkan_last_error), "%.90s", vulkan_note);
            vulkan_close();
            vulkan_give_up();
            return;
        }
        vulkan_renewals++;
        vulkan_failures = 4; /* (a frame that fails now counts on from here) */
        return;
    }
    VkSurfaceKHR surface = vulkan_surface(vk_get);
    if (surface == VK_NULL_HANDLE) {
        vulkan_failures++;
        vulkan_renew_failures++;
        snprintf(vulkan_last_error, sizeof(vulkan_last_error), "no surface: %.70s", SDL_GetError());
        SDL_Log("play: frame %u: no new Vulkan surface (%s)", frame_now, SDL_GetError());
        return;
    }
    int w, h;
    output_size(&w, &h);
    int made = vk_present_surface(presenter, surface, w, h);
    if (made == 0) {
        SDL_Log("play: frame %u: a new Vulkan surface, %d x %d", frame_now, w, h);
        vulkan_lost = 0;
        vulkan_failures = 0;
        vulkan_renewals++;
    } else {
        /* (1: nothing to draw on yet, a window of no size) */
        vulkan_failures += made < 0;
        vulkan_renew_failures += made < 0;
        snprintf(vulkan_last_error, sizeof(vulkan_last_error), "%s", vk_present_error(presenter));
        SDL_Log("play: frame %u: the new Vulkan surface cannot be drawn on (%s)", frame_now, vk_present_error(presenter));
        vk_present_surface_gone(presenter);
    }
}

static void draw(void)
{
    /* (the presenter is made and unmade on this thread only: it can be asked for here) */
    if (vulkan_lose_frame != 0 && (frame_now == vulkan_lose_frame || frame_now == vulkan_lose_frame + 30)) {
        /* --vulkan-lose: the events of an Android app's leaving the screen, and half a second
           later those of its return, as SDL sends them (a "device reset" among them) */
        SDL_Event leave = {.type = frame_now == vulkan_lose_frame ? SDL_APP_WILLENTERBACKGROUND : SDL_APP_DIDENTERFOREGROUND};
        SDL_PushEvent(&leave);
        if (frame_now != vulkan_lose_frame) {
            SDL_Event reset = {.type = SDL_RENDER_DEVICE_RESET};
            SDL_PushEvent(&reset);
        }
    }
    /* a new surface once the app is on the screen again: while it is leaving, the window is
       still there for a moment, and a surface made on it then would go with it */
    if (presenter != NULL && vulkan_lost && !app_away) {
        SDL_LockMutex(vk_lock);
        vulkan_renew();
        SDL_UnlockMutex(vk_lock);
    }
    if (presenter != NULL) {
        if (!vulkan_lost) {
            draw_vulkan();
        }
        return;
    }
    if (shown_frame != drawn_frame) {
        SDL_LockMutex(shown_lock);
        SDL_UpdateTexture(screens, NULL, shown, PICTURE_W * (int) sizeof(uint32_t));
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
    if (window_shot_at(frame_now)) {
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
    output_size(&pw, &ph);
    if (ww > 0 && wh > 0) {
        *x = *x * pw / ww;
        *y = *y * ph / wh;
    }
}

static void pen_set(int down, int x, int y)
{
    if (down && !pen_down && typer.open) {
        typer.anew = 1; /* the player's pen on the game's screen: its text may be changed by it */
        if (!SDL_IsScreenKeyboardShown(window) && y < 88) {
            SDL_StartTextInput(); /* the device's keyboard was put away: a touch on the text brings it back */
        }
    }
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

/* A finger on the touch screen, at (x, y) in the window's pixels. */
static void finger(TouchAction action, int64_t id, int x, int y)
{
    TouchPen pen = touch_finger(&layout, action, id, x, y, frame_now);
    if (pen.toggle) {
        toggle_buttons();
    }
    if (pen.arranged) {
        settings_save();
    }
    if (pen.settings) {
        open_settings();
    }
    if (pen.save) {
        save_now();
    }
    if (pen.changed) {
        int px = 0, py = 0;
        if (pen.down) {
            pen_at(pen.x, pen.y, 1, &px, &py);
        }
        pen_set(pen.down, px, py);
    }
    keys_taps |= touch_taps();
}

/* --touch-script FILE: fingers played from a file, to try the touch screen's handling
 * without one. Lines of "FRAME down|move|up FINGER X Y", in the window's pixels and in the
 * order of their frames. */
static FILE *touch_script;
static char touch_script_line[128];

static void touch_script_play(uint32_t frame)
{
    while (touch_script != NULL) {
        unsigned at, id;
        int x, y;
        char what[8];
        if (touch_script_line[0] == 0 && fgets(touch_script_line, sizeof(touch_script_line), touch_script) == NULL) {
            fclose(touch_script);
            touch_script = NULL;
            return;
        }
        if (sscanf(touch_script_line, "%u %7s %u %d %d", &at, what, &id, &x, &y) != 5) {
            touch_script_line[0] = 0; /* a comment or an empty line */
            continue;
        }
        if (at > frame) {
            return;
        }
        finger(strcmp(what, "down") == 0 ? TOUCH_DOWN : strcmp(what, "up") == 0 ? TOUCH_UP : TOUCH_MOVE, id, x, y);
        touch_script_line[0] = 0;
    }
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
/* Gamepads -------------------------------------------------------------------------------------
 *
 * Which of a gamepad's buttons is which of the DS's: "pad.NAME=BUTTON[,BUTTON...]" lines of
 * the settings, NAME one of the DS's buttons (a b x y l r start select up down left right)
 * and BUTTON one of a gamepad's by SDL's names for them, which go by where a button is and
 * not by what is printed on it (a: the lower of the four, b: the right one, x: the left one,
 * y: the upper one; l1 r1 the shoulders, l2 r2 the triggers, l3 r3 the sticks pressed; start,
 * select, guide; up down left right the pad's own). Without a line, a DS button is the
 * gamepad's in the same place (the DS's A is on the right, as a gamepad's b), with the
 * triggers as L and R too. The left stick is the +Control Pad besides, always. */
enum {
    PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L1, PAD_R1, PAD_L2, PAD_R2, PAD_L3, PAD_R3, PAD_START, PAD_SELECT, PAD_GUIDE,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_SOURCES
};
static const char *const pad_source_names[PAD_SOURCES] = {"a", "b", "x", "y", "l1", "r1", "l2", "r2", "l3", "r3", "start", "select", "guide",
                                                          "up", "down", "left", "right"};
static const struct {
    const char *name;
    uint32_t key;
    const char *sources; /* without a line of the settings */
} pad_ds[] = {
    {"a", DS_KEY_A, "b"},         {"b", DS_KEY_B, "a"},           {"x", DS_KEY_X, "y"},        {"y", DS_KEY_Y, "x"},
    {"l", DS_KEY_L, "l1,l2"},     {"r", DS_KEY_R, "r1,r2"},       {"start", DS_KEY_START, "start"},
    {"select", DS_KEY_SELECT, "select"}, {"up", DS_KEY_UP, "up"}, {"down", DS_KEY_DOWN, "down"},
    {"left", DS_KEY_LEFT, "left"}, {"right", DS_KEY_RIGHT, "right"},
};
#define PAD_DS ((int) (sizeof(pad_ds) / sizeof(pad_ds[0])))
static uint32_t pad_map[PAD_SOURCES]; /* the DS's buttons each of a gamepad's presses */
static int pad_map_made;

/* A DS button's gamepad buttons, as a list of their names. */
static void pad_map_set(int ds, const char *sources)
{
    for (int i = 0; i < PAD_SOURCES; i++) {
        pad_map[i] &= ~pad_ds[ds].key;
    }
    while (*sources != 0) {
        size_t n = strcspn(sources, ",\r\n ");
        for (int i = 0; i < PAD_SOURCES; i++) {
            if (strlen(pad_source_names[i]) == n && strncmp(pad_source_names[i], sources, n) == 0) {
                pad_map[i] |= pad_ds[ds].key;
            }
        }
        sources += n;
        sources += strspn(sources, ",\r\n ");
    }
}

static void pad_map_defaults(void)
{
    memset(pad_map, 0, sizeof(pad_map));
    for (int ds = 0; ds < PAD_DS; ds++) {
        pad_map_set(ds, pad_ds[ds].sources);
    }
    pad_map_made = 1;
}

/* A line of the settings: 1 if it was one of the gamepad's. */
static int pad_settings_line(const char *line)
{
    if (strncmp(line, "pad.", 4) != 0) {
        return 0;
    }
    if (!pad_map_made) {
        pad_map_defaults();
    }
    for (int ds = 0; ds < PAD_DS; ds++) {
        size_t n = strlen(pad_ds[ds].name);
        if (strncmp(line + 4, pad_ds[ds].name, n) == 0 && line[4 + n] == '=') {
            pad_map_set(ds, line + 4 + n + 1);
        }
    }
    return 1;
}

static uint32_t pad_keys(void)
{
    static const SDL_GameControllerButton buttons[PAD_SOURCES] = {
        [PAD_A] = SDL_CONTROLLER_BUTTON_A,           [PAD_B] = SDL_CONTROLLER_BUTTON_B,
        [PAD_X] = SDL_CONTROLLER_BUTTON_X,           [PAD_Y] = SDL_CONTROLLER_BUTTON_Y,
        [PAD_L1] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER, [PAD_R1] = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        [PAD_L2] = SDL_CONTROLLER_BUTTON_INVALID,    [PAD_R2] = SDL_CONTROLLER_BUTTON_INVALID,
        [PAD_L3] = SDL_CONTROLLER_BUTTON_LEFTSTICK,  [PAD_R3] = SDL_CONTROLLER_BUTTON_RIGHTSTICK,
        [PAD_START] = SDL_CONTROLLER_BUTTON_START,   [PAD_SELECT] = SDL_CONTROLLER_BUTTON_BACK,
        [PAD_GUIDE] = SDL_CONTROLLER_BUTTON_GUIDE,   [PAD_UP] = SDL_CONTROLLER_BUTTON_DPAD_UP,
        [PAD_DOWN] = SDL_CONTROLLER_BUTTON_DPAD_DOWN, [PAD_LEFT] = SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        [PAD_RIGHT] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    };
    if (!pad_map_made) {
        pad_map_defaults();
    }
    uint32_t keys = 0;
    int any = 0;
    for (int i = 0; i < 4; i++) {
        if (pads[i] == NULL) {
            continue;
        }
        any = 1;
        const int dead = 16000;
        for (int source = 0; source < PAD_SOURCES; source++) {
            int pressed = buttons[source] != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(pads[i], buttons[source]);
            if (source == PAD_L2 || source == PAD_R2) {
                pressed = SDL_GameControllerGetAxis(pads[i], source == PAD_L2 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT
                                                                             : SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > dead;
            }
            if (pressed) {
                keys |= pad_map[source];
                pad_used = 1;
            }
        }
        int x = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTX);
        int y = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTY);
        uint32_t stick = (x < -dead ? DS_KEY_LEFT : x > dead ? DS_KEY_RIGHT : 0) | (y < -dead ? DS_KEY_UP : y > dead ? DS_KEY_DOWN : 0);
        keys |= stick;
        pad_used |= stick != 0;
    }
    if (!any) {
        pad_used = 0; /* (the buttons on the screen come back when the gamepad is gone) */
    }
    return keys;
}

/* --pad-script FILE: a gamepad played from a file, to try the gamepad's handling without one
 * (SDL's virtual gamepad). Lines of "FRAME BUTTON 1|0", BUTTON by the names above, in the
 * order of their frames. */
static FILE *pad_script;
static char pad_script_line[64];
static SDL_Joystick *pad_script_stick;

static void pad_script_play(uint32_t frame)
{
    while (pad_script != NULL) {
        unsigned at;
        int state;
        char name[16];
        if (pad_script_stick == NULL) {
            int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
            pad_script_stick = index >= 0 ? SDL_JoystickOpen(index) : NULL;
            if (pad_script_stick == NULL) {
                SDL_Log("play: no gamepad for --pad-script (%s)", SDL_GetError());
                fclose(pad_script);
                pad_script = NULL;
                return;
            }
        }
        if (pad_script_line[0] == 0 && fgets(pad_script_line, sizeof(pad_script_line), pad_script) == NULL) {
            fclose(pad_script);
            pad_script = NULL;
            return;
        }
        if (sscanf(pad_script_line, "%u %15s %d", &at, name, &state) != 3) {
            pad_script_line[0] = 0; /* a comment or an empty line */
            continue;
        }
        if (at > frame) {
            return;
        }
        static const int buttons[PAD_SOURCES] = {
            SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, -1, -1, SDL_CONTROLLER_BUTTON_LEFTSTICK,
            SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE,
            SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
            SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        };
        for (int i = 0; i < PAD_SOURCES; i++) {
            if (strcmp(name, pad_source_names[i]) != 0) {
                continue;
            }
            if (i == PAD_L2 || i == PAD_R2) {
                SDL_JoystickSetVirtualAxis(pad_script_stick, i == PAD_L2 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
                                           (Sint16) (state ? 32767 : -32768));
            } else {
                SDL_JoystickSetVirtualButton(pad_script_stick, buttons[i], (Uint8) (state != 0));
            }
        }
        pad_script_line[0] = 0;
    }
}

static void screenshot(void)
{
    static uint32_t copy[sizeof(shown) / sizeof(shown[0])];
    if (presenter != NULL) {
        /* through Vulkan the picture is the window's: its next frame is saved (window-N.png) */
        opt.window_shot = frame_now + 1;
        opt.window_shots = 1;
        SDL_Log("play: screenshot of the window at frame %u (window-%05u.png)", frame_now + 1, frame_now + 1);
        return;
    }
    SDL_LockMutex(shown_lock);
    memcpy(copy, shown, sizeof(copy));
    SDL_UnlockMutex(shown_lock);
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char path[PATH_SIZE + 64];
    snprintf(path, sizeof(path), "%s/acww-%04d%02d%02d-%02d%02d%02d.png", opt.data, t->tm_year + 1900, t->tm_mon + 1,
             t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    SDL_Log(png_write(path, copy, PICTURE_W, PICTURE_H) == 0 ? "play: screenshot %s" : "play: cannot write %s",
            path);
}

static void handle(const SDL_Event *e)
{
    int x, y;
    switch (e->type) {
        case SDL_QUIT:
            quit_asked = 1;
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            app_away = 0;
            settings_apply(); /* the settings screen may have changed them */
            break;
        case SDL_RENDER_DEVICE_RESET:
            /* SDL's renderer lost its device. Not with the window drawn through Vulkan: there
               is no renderer then, yet SDL on Android sends this at every return to the
               screen (it finds no OpenGL context to restore), and a renderer made here would
               turn the Vulkan window into an OpenGL one, on which no surface can be made
               again: the black screen, and Vulkan given up, after the settings. */
            if (presenter != NULL || renderer == NULL) {
                break;
            }
            SDL_Log("play: the graphics device was lost; a new renderer");
            if (open_renderer() != 0) {
                SDL_Log("play: no renderer (%s)", SDL_GetError());
                quit_asked = 1;
            }
            break;
        case SDL_RENDER_TARGETS_RESET:
            if (sharp != NULL) { /* made again when next needed */
                SDL_DestroyTexture(sharp);
                sharp = NULL;
            }
            break;
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_TERMINATING:
            save_flush("the app went to the background");
            autosave_leaving();
            wifi_keep();
            vulkan_marker_set(0);
            app_away = e->type == SDL_APP_WILLENTERBACKGROUND;
            if (e->type == SDL_APP_WILLENTERBACKGROUND && presenter != NULL) {
                /* Android takes the window from under the surface: let go of first (after
                   the frame being drawn, if one is) */
                SDL_LockMutex(vk_lock);
                vulkan_release("the app leaves the screen");
                SDL_UnlockMutex(vk_lock);
            }
            break;
        case SDL_TEXTINPUT:
            if (typer.open) {
                typer_text(e->text.text);
            }
            break;
        case SDL_KEYDOWN:
            if (typer_key(&e->key.keysym, 1)) {
                break;
            }
            if (e->key.keysym.sym == SDLK_TAB) {
                fast_keyboard = 1;
            } else if (e->key.keysym.sym == SDLK_AC_BACK && !e->key.repeat) {
                /* Android's Back: out of the arranging of the buttons, else "leave the game?" */
                if (touch_arrange_close()) {
                    settings_save();
                } else {
                    back_pressed();
                }
            } else if (e->key.keysym.sym == SDLK_F12 && !e->key.repeat) {
                screenshot();
            } else if (e->key.keysym.sym == SDLK_F5 && !e->key.repeat) {
                save_now();
            } else {
                uint32_t k = key_of(e->key.keysym.sym);
                keys_held_keyboard |= k;
                keys_taps |= k;
            }
            break;
        case SDL_KEYUP:
            if (typer_key(&e->key.keysym, 0)) {
                break;
            }
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
            output_size(&w, &h);
            x = (int) (e->tfinger.x * (float) w);
            y = (int) (e->tfinger.y * (float) h);
            finger(e->type == SDL_FINGERDOWN ? TOUCH_DOWN : e->type == SDL_FINGERUP ? TOUCH_UP : TOUCH_MOVE,
                   (int64_t) e->tfinger.fingerId, x, y);
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

/* The picture put together from the frame the runtime finished (both screens at the DS's
 * size) and what the copies of the engines drew for it (tall3d.h, mix2d.h):
 *
 *   - the upper screen, enlarged, with the 3D scene's upper part over it where the tall view
 *     has something to show: opaque where the scene is, blended at its translucent pixels
 *     and smoothed edges, with the main screen's brightness so fades go together;
 *   - the lower screen: at the DS's size the runtime's own; larger, the 3D scene's lower part
 *     mixed with the game's 2D layers by the 2D engine's three answers (mix2d.h).
 */
static uint32_t six_to_eight(uint32_t c)
{
    return c << 2 | c >> 4;
}

static void put_together(const uint32_t *upper, const uint32_t *lower)
{
    const int n = picture_scale, w = PICTURE_W, half = FRONT_SCREEN_H * n;
    const uint32_t *up3 = NULL, *low3 = NULL, *black = NULL, *white = NULL, *none = NULL;
    int stride = 0;
    uint32_t bright = 0;
    int have = tall3d_picture(&up3, &low3, &stride, &bright);
    int mix = have && low3 != NULL && mix2d_answers(&black, &white, &none);
    /* the main engine's master brightness: mode 1 towards white, 2 towards black, by factor/16 */
    uint32_t mode = (bright >> 14) & 3, factor = bright & 31;
    factor = factor > 16 ? 16 : factor;

    for (int y = 0; y < half; y++) {
        const uint32_t *from = upper + (y / n) * FRONT_SCREEN_W, *scene = have && up3 != NULL ? up3 + y * stride : NULL;
        uint32_t *to = shown + y * w;
        for (int x = 0; x < w; x++) {
            uint32_t out = from[x / n], p = scene != NULL ? scene[x] : 0, a = p >> 24;
            if (a != 0) {
                uint32_t mixed = 0;
                for (int k = 0; k < 3; k++) { /* red, green, blue */
                    uint32_t c = (p >> (8 * k)) & 63, old = (out >> (16 - 8 * k)) & 255;
                    if (mode == 1) {
                        c += (63 - c) * factor / 16;
                    } else if (mode == 2) {
                        c -= c * factor / 16;
                    }
                    c = six_to_eight(c);
                    mixed |= (a >= 31 ? c : (c * a + old * (31 - a)) / 31) << (16 - 8 * k);
                }
                out = mixed;
            }
            to[x] = out;
        }
    }
    for (int y = 0; y < half; y++) {
        int line = (y / n) * FRONT_SCREEN_W;
        uint32_t *to = shown + (half + y) * w;
        if (!mix) {
            for (int x = 0; x < w; x++) {
                to[x] = lower[line + x / n];
            }
            continue;
        }
        const uint32_t *scene = low3 + y * stride;
        for (int x = 0; x < w; x++) {
            uint32_t p = scene[x], a = p >> 24, b = black[line + x / n], wh = white[line + x / n], nn = none[line + x / n];
            uint32_t out = 0;
            for (int k = 0; k < 3; k++) {
                int32_t without = (int32_t) ((nn >> (6 * k)) & 63), value = without;
                if (a != 0) {
                    int32_t lo = (int32_t) ((b >> (6 * k)) & 63), hi = (int32_t) ((wh >> (6 * k)) & 63);
                    int32_t c = (int32_t) ((p >> (8 * k)) & 63);
                    value = lo + ((hi - lo) * c + 31) / 63;
                    if (a < 31) {
                        value = (without * (int32_t) (31 - a) + value * (int32_t) a + 15) / 31;
                    }
                    value = value < 0 ? 0 : value > 63 ? 63 : value;
                }
                out |= six_to_eight((uint32_t) value) << (16 - 8 * k);
            }
            to[x] = out;
        }
    }
}

/* --scene-dump: the 3D scene of each --shot frame written beside it, for trying a renderer
 * on it (scene_file.h): scene-N.bin, the scene as the geometry engine left it. Without the
 * option, at a larger size, tiles-N.bin: the 3D picture alone as the software tiles drew it,
 * which a renderer's picture of the same frame is compared with. */
/* The scene engine's thread hands a scene over: kept until the frame it belongs to is put out. */
static void scene_keep(const Scene *scene, void *user)
{
    (void) user;
    Scene *copy = scene_copy(scene);
    SDL_LockMutex(scene_lock);
    scene_free(scene_latest);
    scene_latest = copy;
    SDL_UnlockMutex(scene_lock);
}

/* The engines set up for how the picture is made: the scene handed out whole (for the dump
 * or for the GPU) in place of the tiles, or the tiles at the picture's size. Before the run,
 * and again at its first frame if Vulkan turns out not to be there (open_window). */
static void picture_setup(void)
{
    picture_scale = opt.resolution;
    if (scene_dump || opt.vulkan) {
        if (scene_lock == NULL) {
            scene_lock = SDL_CreateMutex();
        }
        picture_scale = 1;
        tall3d_enable_scene(opt.tall, scene_keep, NULL);
    } else {
        tall3d_enable(opt.tall, picture_scale);
    }
    mix2d_enable(picture_scale > 1 || opt.vulkan);
}

/* On the drawing thread, as a frame is put out (under shown_lock): the scene that arrived
 * since the last one is this frame's, and what a frame through Vulkan needs is put by. */
static void scene_frame(const uint32_t *upper, const uint32_t *lower)
{
    if (scene_lock == NULL) {
        return;
    }
    SDL_LockMutex(scene_lock);
    Scene *arrived = scene_latest;
    scene_latest = NULL;
    SDL_UnlockMutex(scene_lock);
    scene_outputs++;
    scene_arrivals += arrived != NULL;
    if (arrived != NULL) {
        /* The game's new scene. Its scenes are a fixed number of frames apart as the game
           counts them (three in town: twenty a second), but come a frame early or late as
           its work for one ends before or after a frame's end: two, four, two, four frames
           apart for stretches. */
        static uint32_t newest_frame;
        static int newest_tall, newest_known;
        static uint8_t lately[8];
        static unsigned lately_count;
        uint32_t apart = newest_known ? scene_outputs - scene_last_output : 0;
        int follows = newest_known && arrived->frame == newest_frame + 1 && arrived->tall == newest_tall && apart >= 1 && apart <= 6;
        scene_every[apart < 8 ? apart : 8]++;
        scene_last_output = scene_outputs;
        newest_frame = arrived->frame;
        newest_tall = arrived->tall;
        newest_known = 1;
        if (follows) {
            /* how far apart they usually are: the mean of the last eight */
            unsigned sum = 0, n = 0;
            lately[lately_count++ % 8] = (uint8_t) apart;
            for (; n < (lately_count < 8 ? lately_count : 8); n++) {
                sum += lately[n];
            }
            scene_usual = (sum + n / 2) / n;
            scene_usual = scene_usual < 2 ? 2 : scene_usual > 4 ? 4 : scene_usual;
        }
        if (!(fps60_setting && opt.vulkan && !scene_dump) || !follows || scene_last == NULL) {
            /* at once, as the game has it: sixty frames is off, or this is not the scene
               after the last one (a cut, a pause) */
            scene_free(scene_from);
            scene_free(scene_wait);
            scene_free(scene_last);
            scene_from = scene_wait = NULL;
            scene_last = arrived;
            scene_steps = scene_step = 1;
        } else if (scene_wait != NULL) {
            /* one is waiting already: the window is behind the game. The scene on its way
               is left where it is for the one that waited. */
            scene_free(scene_from);
            scene_from = scene_last;
            scene_last = scene_wait;
            scene_wait = arrived;
            scene_step = 1;
            scene_steps = scene_usual;
        } else {
            scene_wait = arrived;
        }
    }
    /* Sixty frames: the window shows the way from each scene to the next, a step a frame, in
       as many frames as the scenes are usually apart, one scene after the other at an even
       pace whichever frame each came in: the next waits until the way to the last one is
       done. The 3D picture is two or three frames behind the game for it, and moves in
       sixtieths of a second. */
    if (scene_last != NULL && scene_step > scene_steps && scene_wait != NULL) {
        scene_held += scene_idle <= 3 ? scene_idle : 0; /* (longer: the game was not drawing) */
        scene_idle = 0;
        scene_free(scene_from);
        scene_from = scene_last;
        scene_last = scene_wait;
        scene_wait = NULL;
        scene_step = 1;
        scene_steps = scene_usual;
    }
    if (scene_last != NULL && scene_step <= scene_steps) {
        Scene *show = NULL;
        if (scene_step < scene_steps && scene_from != NULL) {
            int moved = 0, of = 0;
            show = scene_between(scene_from, scene_last, (float) scene_step / (float) scene_steps, &moved, &of);
            if (tween_check && scene_step == 1) {
                int checked = 0, wrong = scene_between_same(scene_from, scene_last, &checked);
                tween_checked += (unsigned long) checked;
                tween_wrong += (unsigned long) wrong;
            }
            if (show != NULL) {
                tween_scenes++;
                tween_moved += (unsigned long) moved;
                tween_polygons += (unsigned long) of;
            }
        }
        if (show == NULL) {
            show = scene_hold(scene_last); /* the scene itself: its turn, or nothing could be made */
            scene_step = scene_steps;
        }
        scene_free(scene_current);
        scene_current = show;
        scene_step++;
    } else if (scene_last != NULL) {
        scene_idle++; /* the way is done and the next scene has not come: the last one again */
    }
    if (!opt.vulkan) {
        return;
    }
    const uint32_t *black = NULL, *white = NULL, *none = NULL;
    memcpy(packet.upper, upper, sizeof(packet.upper));
    memcpy(packet.lower, lower, sizeof(packet.lower));
    packet.answers = mix2d_answers(&black, &white, &none);
    if (packet.answers) {
        memcpy(packet.black, black, sizeof(packet.black));
        memcpy(packet.white, white, sizeof(packet.white));
        memcpy(packet.none, none, sizeof(packet.none));
    }
    packet.scene_shown = tall3d_scene_state(&packet.over_upper, &packet.brightness);
}

static void scene_shot(uint32_t frame)
{
    char path[PATH_SIZE + 64];
    if (scene_dump) {
        snprintf(path, sizeof(path), "%s/scene-%05u.bin", opt.data, frame);
        if (scene_current != NULL) {
            scene_write(path, scene_current);
        }
        return;
    }
    const uint32_t *upper = NULL, *lower = NULL;
    int stride = 0;
    uint32_t bright = 0;
    if (picture_scale > 1 && tall3d_picture(&upper, &lower, &stride, &bright) && lower != NULL) {
        /* the width, the height, then the rows from the top: 6-bit red, green and blue in
           bits 0-5, 8-13 and 16-21, and the alpha, 0 to 31, in bits 24-28 */
        uint32_t w = (uint32_t) PICTURE_W, h = (uint32_t) (upper != NULL ? PICTURE_H : PICTURE_H / 2);
        snprintf(path, sizeof(path), "%s/tiles-%05u.bin", opt.data, frame);
        FILE *f = fopen(path, "wb");
        if (f != NULL) {
            fwrite(&w, 4, 1, f);
            fwrite(&h, 4, 1, f);
            for (uint32_t y = 0; y < h; y++) {
                const uint32_t *row = upper != NULL ? upper + y * (uint32_t) stride : lower + y * (uint32_t) stride;
                fwrite(row, 4, w, f);
            }
            fclose(f);
        }
    }
}

/* --picture-hash FILE: a line for each frame's picture as the port shows it (the tall view
 * and the larger scene included, which the decomp's own hashes, --hash, do not cover) */
static void on_frame_output(uint32_t frame, const uint32_t *upper, const uint32_t *lower)
{
    SDL_LockMutex(shown_lock);
    put_together(upper, lower);
    scene_frame(upper, lower);
    shown_frame = frame + 1;
    if (picture_hashes != NULL) {
        uint64_t h = 1469598103934665603ull; /* FNV-1a over the pixels */
        for (int i = 0; i < PICTURE_W * PICTURE_H; i++) {
            h = (h ^ shown[i]) * 1099511628211ull;
        }
        fprintf(picture_hashes, "%u %016llx\n", frame, (unsigned long long) h);
    }
    for (int i = 0; i < opt.shot_count; i++) {
        if (opt.shots[i] == frame) {
            char path[PATH_SIZE + 64];
            snprintf(path, sizeof(path), "%s/frame-%05u.png", opt.data, frame);
            png_write(path, shown, PICTURE_W, PICTURE_H);
            scene_shot(frame);
        }
    }
    SDL_UnlockMutex(shown_lock);
    /* through Vulkan: this picture's turn in the window (see `job`) */
    if (job.thread != NULL) {
        SDL_LockMutex(job.lock);
        if (job.by_output) {
            job.output_frame = frame;
            job.output_posts++;
            job.posted++;
            SDL_CondBroadcast(job.wake);
        }
        SDL_UnlockMutex(job.lock);
    }
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
        char sound[160];
        port_sound_status(sound, sizeof(sound));
        SDL_Log("play: frame %u, %.2f fps (the DS: %.2f), emulation busy %.0f%% of the time%s; sound: %s", frame_now, fps,
                DS_BUS_HZ / DS_CYCLES_PER_FRAME, busy * 100.0, fast ? ", fast forward" : "", opt.sound ? sound : "off");
        if (!fast) {
            status_fps = fps;
            status_busy = busy;
        }
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
        game_hooks_resetti(opt.resetti);
        game_keyboard_enable(type_setting);
        arrange_take(); /* asked for just before the app was closed */
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
    touch_script_play(frame);
    pad_script_play(frame);
    if (touch_held(frame)) {
        opt.full_touch = 0; /* the mark was held: the buttons are shown to be arranged */
    }

    /* the input for this frame: what is held, and presses (and touches) that began and ended
       since the last frame, held for two frames so the game's reader sees them */
    uint32_t taps = keys_taps;
    keys_taps = 0;
    keys_held_pad = pad_keys();
    uint32_t keys = keys_held_keyboard | keys_held_pad | touch_keys() | taps | taps_last;
    taps_last = taps;
    int down = pen_down, x = pen_x, y = pen_y;
    typer_frame(frame, &down, &x, &y);
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
        autosave_game_saved();
    }
    autosave_frame_step(frame);
    if (frame % 120 == 0) {
        wifi_keep();
    }

    /* quitting: after the game's writes to the chip have settled (at most 3 seconds more) */
    if (quit_asked || (opt.stop_frame && frame >= opt.stop_frame)) {
        if (writes == save_writes_flushed || save_quiet_frames >= 30 || ++quit_wait > 180) {
            save_flush(quit_asked ? "quit" : "frame limit");
            wifi_keep();
            SDL_Log("play: %lu drawings of objects above the game's screen", game_hooks_drawn_above());
            SDL_Log("play: Mr. Resetti kept away %lu times", game_hooks_resetti_kept_away());
            if (opt.vulkan) {
                unsigned long left, drawn, lines, kept, wrong;
                tall3d_lean_counts(&left, &drawn);
                gpu2d_main_probe_counts(&lines, &kept, &wrong);
                SDL_Log("play: the game's own 3D picture: %lu left undrawn, %lu drawn; the 2D mix: %lu lines, %lu kept from the "
                        "frame before%s",
                        left, drawn, lines, kept, getenv("PORT_MIX_CHECK") != NULL ? (wrong ? ", SOME OF THEM WRONG" : ", all of them right") : "");
                unsigned long how[5];
                scene_between_counts(how);
                if (typer.keys) {
                    SDL_Log("play: typing: %lu keys on the game's keyboard in %lu sessions, %lu refused, %lu times Enter for the screen's button",
                            typer.keys, typer.sessions, typer.refused, typer.confirms);
                }
                SDL_Log("play: the game drew %lu scenes in %u frames; frames after the one before: 2: %lu, 3: %lu, 4: %lu, 5: %lu, more: %lu; "
                        "frames that waited for one: %lu",
                        scene_arrivals, scene_outputs, scene_every[2], scene_every[3], scene_every[4], scene_every[5],
                        scene_every[6] + scene_every[7] + scene_every[8], scene_held);
                if (tween_check) {
                    SDL_Log("play: sixty frames: of %lu polygons found in pairs of scenes with the same order, %lu in another's place",
                            tween_checked, tween_wrong);
                }
                if (tween_scenes) {
                    SDL_Log("play: sixty frames: %lu scenes made between the game's, %lu of their %lu polygons moved (%lu with a "
                            "flowing texture; not: %lu not noted, %lu new, %lu too far)",
                            tween_scenes, tween_moved, tween_polygons, how[1], how[2], how[3], how[4]);
                }
            }
            SDL_Log("play: ended at frame %u", frame);
            vulkan_marker_set(0);
            /* the drawing thread's last frames, for those who asked for a record of them */
            if (picture_hashes != NULL || (opt.hash != NULL && *opt.hash)) {
                render_finish();
            }
            /* not exit(): the drawing thread is still running, and exit() takes apart what it
               uses (Android's C library aborts on a mutex destroyed under a thread) */
            fflush(NULL);
            _exit(0);
        }
    }
    /* fast forward: the window every tenth frame is enough, and the drawing would be the
       slowest part of it */
    if (!(fast_keyboard || opt.unpaced) || frame % 10 == 0 || window_shot_at(frame)) {
        draw();
    }
    pace();
}

/* main -------------------------------------------------------------------------------------- */

static void set_env(const char *name, const char *value)
{
    /* nothing to say: the variable is taken away, not left empty. The runtime asks of some
       only whether they exist (RUN_DUMP3D, which turns its drawing thread off) */
    if (value == NULL || *value == 0) {
        unsetenv(name);
        return;
    }
    SDL_setenv(name, value, 1);
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
    opt.sound = 1;
    opt.autosave = 1;
    opt.resolution = 1;
    default_data_dir(opt.data);
    const char *save = NULL, *rom = NULL;
    const char *audio_wav = NULL;
    int autosave = -1, load_autosave = 0, vulkan = -1, link = -1, fps60 = -1, type_keyboard = -1;
    const char *link_server = NULL, *link_room = NULL, *wifi_dns = NULL;
    int wifi = -1;
    int full_touch = -1, resetti = -1, two_screens = 0, no_sound = 0, resolution = 0; /* as the settings say, unless the command line does */
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
        } else if (strcmp(a, "--resolution") == 0 && more) {
            resolution = atoi(argv[++i]);
        } else if (strcmp(a, "--no-sound") == 0) {
            no_sound = 1;
        } else if (strcmp(a, "--audio-wav") == 0 && more) {
            audio_wav = argv[++i];
        } else if (strcmp(a, "--autosave") == 0) {
            autosave = 1;
        } else if (strcmp(a, "--no-autosave") == 0) {
            autosave = 0;
        } else if (strcmp(a, "--load-autosave") == 0) {
            load_autosave = 1;
        } else if (strcmp(a, "--resetti") == 0) {
            resetti = 1;
        } else if (strcmp(a, "--no-resetti") == 0) {
            resetti = 0;
        } else if (strcmp(a, "--pad-script") == 0 && more) {
            pad_script = fopen(argv[++i], "r");
        } else if (strcmp(a, "--touch-script") == 0 && more) {
            touch_script = fopen(argv[++i], "r");
        } else if (strcmp(a, "--no-above") == 0) {
            opt.no_above = 1;
        } else if (strcmp(a, "--hash") == 0 && more) {
            opt.hash = argv[++i];
        } else if (strcmp(a, "--picture-hash") == 0 && more) {
            picture_hashes = fopen(argv[++i], "w");
        } else if (strcmp(a, "--full-touch") == 0) {
            full_touch = 1;
        } else if (strcmp(a, "--size") == 0 && more) {
            sscanf(argv[++i], "%dx%d", &opt.width, &opt.height);
        } else if (strcmp(a, "--window-shot") == 0 && more) {
            opt.window_shot = (uint32_t) strtoul(argv[++i], NULL, 10);
        } else if (strcmp(a, "--window-shots") == 0 && more) {
            opt.window_shots = (uint32_t) strtoul(argv[++i], NULL, 10);
        } else if (strcmp(a, "--scene-dump") == 0) {
            scene_dump = 1;
        } else if (strcmp(a, "--link-host") == 0) {
            link = 1;
            link_server = "";
        } else if (strcmp(a, "--link-join") == 0 && more) {
            link = 1;
            link_server = argv[++i];
        } else if (strcmp(a, "--link-room") == 0 && more) {
            link_room = argv[++i];
        } else if (strcmp(a, "--link-port") == 0 && more) {
            opt.link_port = atoi(argv[++i]);
        } else if (strcmp(a, "--no-link") == 0) {
            link = 0;
        } else if (strcmp(a, "--wifi") == 0) {
            wifi = 1;
        } else if (strcmp(a, "--no-wifi") == 0) {
            wifi = 0;
        } else if (strcmp(a, "--wifi-dns") == 0 && more) {
            wifi = 1;
            wifi_dns = argv[++i];
        } else if (strcmp(a, "--vulkan") == 0) {
            vulkan = 1;
            vulkan_explicit = 1;
        } else if (strcmp(a, "--type-keyboard") == 0) {
            type_keyboard = 1;
        } else if (strcmp(a, "--no-type-keyboard") == 0) {
            type_keyboard = 0;
        } else if (strcmp(a, "--type") == 0 && more) {
            /* FRAME:TEXT, for a check: the text as if typed on the device's keyboard at that frame */
            type_script_frame = (uint32_t) strtoul(argv[++i], NULL, 10);
            type_script = strchr(argv[i], ':') != NULL ? strchr(argv[i], ':') + 1 : NULL;
            type_keyboard = 1;
        } else if (strcmp(a, "--no-fps60") == 0) {
            fps60 = 0;
        } else if (strcmp(a, "--fps60") == 0) {
            fps60 = 1;
        } else if (strcmp(a, "--vulkan-lose") == 0 && more) {
            vulkan_lose_frame = (uint32_t) strtoul(argv[++i], NULL, 10);
        } else if (strcmp(a, "--no-vulkan") == 0) {
            vulkan = 0;
        } else if (strcmp(a, "--two-screens") == 0) {
            opt.tall = 0;
            two_screens = 1;
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
    /* before any other thread is started: they are kept to the same cores */
    if (fast_cores_setting && getenv("PORT_ALL_CORES") == NULL && cores_prefer_fast(cores_note, sizeof(cores_note)) > 0) {
        SDL_Log("play: the threads are kept to the fast cores, %s", cores_note);
    }
    if (full_touch >= 0) {
        opt.full_touch = full_touch;
    }
    if (resetti >= 0) {
        opt.resetti = resetti;
    }
    if (autosave >= 0) {
        opt.autosave = autosave;
    }
    opt.vulkan = vulkan >= 0 ? vulkan : vulkan_setting;
    fps60_setting = fps60 >= 0 ? fps60 : fps60_setting;
    type_setting = type_keyboard >= 0 ? type_keyboard : type_setting;
    tween_check = getenv("PORT_TWEEN_CHECK") != NULL;

    opt.link = link >= 0 ? link : opt.link;
    if (link_server != NULL) {
        snprintf(opt.link_server, sizeof(opt.link_server), "%s", link_server);
    }
    if (link_room != NULL) {
        snprintf(opt.link_room, sizeof(opt.link_room), "%s", link_room);
    }
    opt.wifi = wifi >= 0 ? wifi : opt.wifi;
    if (wifi_dns != NULL) {
        snprintf(opt.wifi_dns, sizeof(opt.wifi_dns), "%.63s", wifi_dns);
    }
    if (!two_screens) {
        opt.tall = tall_setting;
    }
    opt.resolution = resolution > 0 ? resolution : resolution_setting;
    opt.resolution = opt.resolution < 1 ? 1 : opt.resolution > TALL3D_SCALE_MAX ? TALL3D_SCALE_MAX : opt.resolution;
    if (no_sound) {
        opt.sound = 0;
    }
#ifdef __ANDROID__
    touch_settings_screen(1);
#endif
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
    autosave_at_start(load_autosave);

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
    /* sound: the runtime's sequencer and mixer, heard through sound_sdl.c */
    set_env("RUN_AUDIO", opt.sound ? "1" : "");
    set_env("RUN_AUDIO_WAV", audio_wav != NULL ? audio_wav : "");
    if (opt.sound) {
        set_env("RUN_SEQUENCER", "1");
    }
    set_env("RUN_HASH", opt.hash);
    /* with others (docs/multiplayer.md): the room is joined now, since the console's number
       in it, which the run starts with, is the relay's to give */
    set_env("RUN_WIRELESS", "");
    set_env("RUN_LINK", "");
    if (opt.link) {
        char why[160] = "";
        int console = netlink_start(opt.link_server, opt.link_room, opt.link_port, why, sizeof(why));
        if (console >= 0) {
            char status[320];
            netlink_status(status, sizeof(status));
            SDL_Log("play: with others: %s", status);
            set_env("RUN_WIRELESS", "1");
            set_env("RUN_LINK", netlink_run_link());
        } else {
            SDL_Log("play: with others: %s; playing alone", why);
            snprintf(link_failure, sizeof(link_failure), "%s", why);
        }
    }
    /* Wi-Fi Connection (docs/multiplayer.md): the access point's network is this machine's.
       Not with a room joined: a console of a room has the room's address */
    arm7_port_on_reset(console_reset);
    wifi_active = opt.wifi && netlink_run_link() == NULL;
    if (wifi_active) {
        wifi_start();
        set_env("RUN_WIRELESS", "1");
    }
    static const char *const cleared[] = {"RUN_PNG",           "RUN_PNG_FRAMES",      "RUN_DUMP3D",   "RUN_NATIVE_RECORD",
                                          "RUN_NATIVE_REPLAY", "RUN_RAM_HASH", "RUN_RAM_DUMP", "RUN_NATIVE_LEARN",
                                          "RUN_CLEAN_RAN",     "RUN_TRACE",    "RUN_CALLS",    "RUN_DRIVE"};
    for (size_t i = 0; i < sizeof(cleared) / sizeof(cleared[0]); i++) {
        set_env(cleared[i], "");
    }
    /* (for looking into the runtime from the front end: PORT_RUN_TRACE=wm, for one) */
    set_env("RUN_TRACE", getenv("PORT_RUN_TRACE"));
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

    picture_setup();
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
