/* Sound out of an SDL audio device.
 *
 * The decomp's runtime renders the DS's sound itself (the sequencer and the mixer,
 * decomp/runtime/sequencer.c and ds_audio.c: 32768 Hz, signed 16-bit stereo), and
 * decomp/runtime/sound.c hands the samples to a device in blocks of 512 frames: Windows's
 * waveOut, in the build with a window, and nothing elsewhere. That code is used here as it
 * is. This file includes it with its device part switched on, and gives it the handful of
 * waveOut names it uses, made of SDL's queue instead:
 *
 *   - a block is queued unless about an eighth of a second is already waiting, which is how
 *     much the decomp's eight buffers hold; then it is dropped, as sound.c counts it. The
 *     emulation is never held up by the device;
 *   - a reset (fast forward begins or ends) empties the queue;
 *   - the device is opened at the first block, not when the runtime starts: by then the DS's
 *     address ranges are reserved, and what the audio driver allocates cannot land in them.
 */
#include <SDL.h>

#include <stdint.h>
#include <stdio.h>

typedef struct PortWaveOut {
    SDL_AudioDeviceID device;
    int rate, failed;
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

#define WHDR_INQUEUE 0x10u /* never set here: a block is copied into SDL's queue at once */
#define MMSYSERR_NOERROR 0u
#define MMSYSERR_ERROR 1u
#define WAVE_FORMAT_PCM 1
#define WAVE_MAPPER 0
#define CALLBACK_NULL 0

/* what may wait in the queue: the decomp's eight blocks of 512 frames of 4 bytes */
#define QUEUE_LIMIT (8u * 512u * 4u)

static struct PortWaveOut wave_out;

static MMRESULT waveOutOpen(HWAVEOUT *out, int which, const WAVEFORMATEX *format, int a, int b, int c)
{
    (void) which;
    (void) a;
    (void) b;
    (void) c;
    wave_out.device = 0;
    wave_out.failed = 0;
    wave_out.rate = (int) format->nSamplesPerSec;
    *out = &wave_out;
    return MMSYSERR_NOERROR;
}

static MMRESULT waveOutWrite(HWAVEOUT out, WAVEHDR *block, unsigned size)
{
    (void) size;
    if (out->device == 0 && !out->failed) {
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = out->rate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 || (out->device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0)) == 0) {
            out->failed = 1;
            fprintf(stderr, "sound: cannot open the audio device (%s)\n", SDL_GetError());
        } else {
            SDL_PauseAudioDevice(out->device, 0);
            fprintf(stderr, "sound: SDL audio, stereo PCM %d Hz (%s)\n", out->rate, SDL_GetCurrentAudioDriver());
        }
    }
    if (out->device == 0 || SDL_GetQueuedAudioSize(out->device) > QUEUE_LIMIT) {
        return MMSYSERR_ERROR;
    }
    return SDL_QueueAudio(out->device, block->lpData, block->dwBufferLength) == 0 ? MMSYSERR_NOERROR : MMSYSERR_ERROR;
}

static MMRESULT waveOutReset(HWAVEOUT out)
{
    if (out->device != 0) {
        SDL_ClearQueuedAudio(out->device);
    }
    return MMSYSERR_NOERROR;
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
