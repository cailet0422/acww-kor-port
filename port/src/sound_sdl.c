/* Sound out of an SDL audio device.
 *
 * The decomp's runtime renders the DS's sound itself (the sequencer and the mixer,
 * decomp/runtime/sequencer.c and ds_audio.c: signed 16-bit stereo, here at the DS's own
 * 32728.5 Hz, ds_audio_port.c), and decomp/runtime/sound.c hands the samples to a device in
 * blocks of 512 frames: Windows's waveOut, in the build with a window, and nothing elsewhere. That code is used here as it
 * is. This file includes it with its device part switched on, and gives it the handful of
 * waveOut names it uses, made of an SDL device instead.
 *
 * Between the two is a ring of the DS's frames and a rate converter of the port's own:
 *
 *   - the device is asked for 48000 Hz and may answer with its own rate. The DS's is not a
 *     rate audio hardware has, and a phone's audio system is not relied on to play one
 *     in tune;
 *   - the device's clock and the emulation's are two clocks, and the emulation's sound comes
 *     a frame at a time. The ring is kept about 80 ms full by playing up to half a percent
 *     faster or slower (9 cents, moving slowly), where a plain queue would run empty or over
 *     every so often and click;
 *   - when the emulation cannot keep up and the ring runs empty, the sound fades out, waits
 *     for the ring to fill again and fades in; when the ring runs over (the emulation ahead
 *     by more than the half percent), the oldest sound is skipped. Neither holds up the
 *     emulation;
 *   - a reset (fast forward begins or ends) empties the ring;
 *   - the device is opened at the first block, not when the runtime starts.
 */
#include <SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct PortWaveOut {
    SDL_AudioDeviceID device;
    double rate;        /* the mixer's frames a second */
    int device_rate, failed;
} *HWAVEOUT;
typedef char *LPSTR;
typedef unsigned MMRESULT;
typedef struct WAVEHDR {
    LPSTR lpData;
    unsigned dwBufferLength;
    unsigned dwFlags;
} WAVEHDR;
typedef struct WAVEFORMATEX {
    unsigned short wFormatTag, nChannels;
    unsigned nSamplesPerSec, nAvgBytesPerSec;
    unsigned short nBlockAlign, wBitsPerSample, cbSize;
} WAVEFORMATEX;

#define WHDR_INQUEUE 0x10u /* never set here: a block is copied into the ring at once */
#define MMSYSERR_NOERROR 0u
#define MMSYSERR_ERROR 1u
#define WAVE_FORMAT_PCM 1
#define WAVE_MAPPER 0
#define CALLBACK_NULL 0

/* The ring: the DS's frames waiting to be played */
#define RING 8192u                 /* frames; a power of two */
#define RING_TARGET 2560           /* frames the ring is kept at when the device takes its turn (78 ms) */
#define RING_LIMIT 6144u           /* more than this waiting: the oldest are skipped */

/* The rate converter: a windowed sinc, TAPS of the DS's frames wide, tabled at PHASES
 * positions between two frames and interpolated between those */
#define TAPS 32
#define PHASES 256
#define RATE_SWAY 0.005            /* how far the rate may follow the ring's level */
#define RATE_SWAY_AT 512           /* frames off its level at which it has gone that far */
#define FADE (1.0f / 128.0f)       /* the gain's step per frame played */

static struct PortWaveOut wave_out;
static int16_t ring[RING][2];
static uint32_t ring_read, ring_write; /* frame counts; the index is the count modulo RING */
static float kernel[PHASES + 1][TAPS];
static double position;            /* where the next frame played lies past ring_read + TAPS / 2 - 1 */
static double level;               /* the ring's level, smoothed */
static float gain;                 /* 0 while the ring fills, 1 while it plays */
static int playing;
static unsigned gaps, skips;
static char device_text[96];

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

/* The kernel for a device at `rate`: a low-pass at the lower of the two rates' halves. */
static void kernel_make(double source, int rate)
{
    const double pi = 3.14159265358979323846, beta = 7.0;
    double cut = rate < source ? rate / source : 1.0;
    for (int p = 0; p <= PHASES; p++) {
        double sum = 0.0, h[TAPS];
        for (int k = 0; k < TAPS; k++) {
            double x = (k - (TAPS / 2 - 1)) - (double) p / PHASES; /* in the DS's frames */
            double w = x / (TAPS / 2);
            double window = w <= -1.0 || w >= 1.0 ? 0.0 : bessel_i0(beta * SDL_sqrt(1.0 - w * w)) / bessel_i0(beta);
            double t = pi * x * cut;
            h[k] = (t == 0.0 ? 1.0 : SDL_sin(t) / t) * cut * window;
            sum += h[k];
        }
        for (int k = 0; k < TAPS; k++) {
            kernel[p][k] = (float) (h[k] / sum);
        }
    }
}

static int16_t rounded(float sample)
{
    return (int16_t) (sample >= 32767.0f ? 32767 : sample <= -32768.0f ? -32768 : (int) (sample + (sample < 0.0f ? -0.5f : 0.5f)));
}

/* The device asks for `bytes` of frames. Runs on SDL's audio thread with the device locked,
 * which is what the emulation's thread takes to touch the ring. */
static void SDLCALL device_pull(void *user, Uint8 *stream, int bytes)
{
    struct PortWaveOut *out = user;
    int16_t *to = (int16_t *) stream;
    int frames = bytes / 4;
    int waiting = (int) (ring_write - ring_read);
    if (!playing && waiting >= RING_TARGET) {
        playing = 1;
        level = waiting;
    }
    if (playing) {
        level += (waiting - level) * ((double) frames / out->device_rate); /* over about a second */
    }
    double sway = RATE_SWAY * (level - RING_TARGET) / RATE_SWAY_AT;
    sway = sway > RATE_SWAY ? RATE_SWAY : sway < -RATE_SWAY ? -RATE_SWAY : sway;
    double step = out->rate / out->device_rate * (1.0 + sway);
    /* frames to keep ahead while the gain comes down before the ring is empty */
    int low = TAPS + 2 + (int) step + (int) (step / FADE) + 1;
    for (int i = 0; i < frames; i++) {
        waiting = (int) (ring_write - ring_read);
        if (playing && waiting < TAPS + 2 + (int) step) {
            playing = 0; /* empty: silence until the ring is at its level again */
            gain = 0.0f;
            gaps++;
        }
        if (!playing) {
            to[2 * i] = to[2 * i + 1] = 0;
            continue;
        }
        float goal = waiting < low ? 0.0f : 1.0f;
        gain += gain < goal ? FADE : gain > goal ? -FADE : 0.0f;
        gain = gain < 0.0f ? 0.0f : gain > 1.0f ? 1.0f : gain;
        double at = position * PHASES;
        int phase = (int) at;
        float between = (float) (at - phase);
        const float *k0 = kernel[phase], *k1 = kernel[phase + 1];
        float left = 0.0f, right = 0.0f;
        for (int k = 0; k < TAPS; k++) {
            const int16_t *frame = ring[(ring_read + (uint32_t) k) & (RING - 1)];
            float weight = k0[k] + (k1[k] - k0[k]) * between;
            left += weight * frame[0];
            right += weight * frame[1];
        }
        left *= gain;
        right *= gain;
        to[2 * i] = rounded(left);
        to[2 * i + 1] = rounded(right);
        position += step;
        uint32_t whole = (uint32_t) position;
        position -= whole;
        ring_read += whole;
    }
}

static MMRESULT waveOutOpen(HWAVEOUT *out, int which, const WAVEFORMATEX *format, int a, int b, int c)
{
    (void) which;
    (void) a;
    (void) b;
    (void) c;
    wave_out.device = 0;
    wave_out.failed = 0;
    /* not what sound.c says (the decomp's 32768): this build's mixer has the DS's grid, a
       frame every 1024 cycles of the bus clock (ds_audio_port.c) */
    (void) format;
    wave_out.rate = 33513982.0 / 1024.0;
    *out = &wave_out;
    return MMSYSERR_NOERROR;
}

static void ring_empty(void)
{
    ring_read = ring_write;
    position = 0.0;
    playing = 0;
    gain = 0.0f;
}

static MMRESULT waveOutWrite(HWAVEOUT out, WAVEHDR *block, unsigned size)
{
    (void) size;
    if (out->device == 0 && !out->failed) {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = 48000;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = device_pull;
        want.userdata = out;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 ||
            (out->device = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE)) == 0) {
            out->failed = 1;
            snprintf(device_text, sizeof(device_text), "none (%.60s)", SDL_GetError());
            fprintf(stderr, "sound: cannot open the audio device (%s)\n", SDL_GetError());
        } else {
            out->device_rate = have.freq;
            kernel_make(out->rate, out->device_rate);
            ring_empty();
            gaps = skips = 0;
            snprintf(device_text, sizeof(device_text), "%s %d Hz", SDL_GetCurrentAudioDriver(), have.freq);
            fprintf(stderr, "sound: SDL audio (%s), the device at %d Hz in buffers of %d frames, the DS's %.1f Hz converted\n",
                    SDL_GetCurrentAudioDriver(), have.freq, have.samples, out->rate);
            SDL_PauseAudioDevice(out->device, 0);
        }
    }
    if (out->device == 0) {
        return MMSYSERR_ERROR;
    }
    uint32_t frames = block->dwBufferLength / 4;
    SDL_LockAudioDevice(out->device);
    if (ring_write - ring_read + frames > RING_LIMIT) {
        /* the emulation is ahead of the device by more than the rate may sway */
        ring_read = ring_write - RING_TARGET;
        skips++;
    }
    for (uint32_t i = 0; i < frames; i++) {
        memcpy(ring[(ring_write + i) & (RING - 1)], block->lpData + 4 * i, 4);
    }
    ring_write += frames;
    SDL_UnlockAudioDevice(out->device);
    return MMSYSERR_NOERROR;
}

static MMRESULT waveOutReset(HWAVEOUT out)
{
    if (out->device != 0) {
        SDL_LockAudioDevice(out->device);
        ring_empty();
        SDL_UnlockAudioDevice(out->device);
    }
    return MMSYSERR_NOERROR;
}

/* The device, its rate and what went wrong so far, for the log and the settings screen. */
void port_sound_status(char *text, size_t size)
{
    if (device_text[0] == 0) {
        snprintf(text, size, "-");
    } else {
        snprintf(text, size, "%s, gaps %u, skips %u", device_text, gaps, skips);
    }
}

static MMRESULT waveOutPrepareHeader(HWAVEOUT out, WAVEHDR *block, unsigned size)
{
    (void) out;
    (void) size;
    block->dwFlags = 0;
    return MMSYSERR_NOERROR;
}

static MMRESULT waveOutUnprepareHeader(HWAVEOUT out, WAVEHDR *block, unsigned size)
{
    (void) out;
    (void) block;
    (void) size;
    return MMSYSERR_NOERROR;
}

static MMRESULT waveOutClose(HWAVEOUT out)
{
    if (out->device != 0) {
        SDL_CloseAudioDevice(out->device);
        out->device = 0;
    }
    return MMSYSERR_NOERROR;
}

#define SOUND_DEVICE 1
#include "sound.c"
