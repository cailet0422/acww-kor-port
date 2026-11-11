/* The decomp's mixer (decomp/runtime/ds_audio.c) with two things of the DS's that Wild World's
 * sound depends on. The file is the decomp's, included below; what is added goes around it.
 *
 * Wild World does not play its channels straight to the speakers. It has the DS capture the
 * mix of them into memory, a thread of the game's changes what was captured (the SDK's
 * "output effect": surround, unless the player chose otherwise), and channels 1 and 3 play
 * that memory. Capture and playback go round the same 1024 samples at the same pace.
 *
 * 1. Data fetched ahead. A DS channel does not read a sample from memory at the moment it
 *    plays it: it holds up to 32 bytes that it read earlier, 16 at a time, whenever half of
 *    them are used up, and 32 at once when it starts. With that, channel 1 reads each place
 *    of the round shortly BEFORE the capture writes it again, so what it plays is what was
 *    captured one round (31 ms) earlier, which the game's thread has had time to change.
 *    Read at the moment of playing, as the decomp's mixer does, each place has just been
 *    captured: the game's effect is always a round too late and never heard, and the sound
 *    comes out as the plain mix, flatter than a DS's.
 *
 * 2. The DS's output grid. Its mixer puts out a sample every 1024 cycles of the bus clock:
 *    33513982 / 1024, about 32728.5 a second, which is also the pace of channels 1 and 3. The
 *    decomp's renders 32768 a second, a rate a WAV file can have, and there one sample in 830
 *    of the played-back mix is heard twice. Here a frame is put out every 1024 cycles;
 *    sound_sdl.c converts that rate to the device's.
 *
 * The front end's mixer is built from this file. The headless test program keeps the decomp's
 * file as it is, and its WAV files the decomp's.
 */
#include <string.h>

#include "ds_audio.h"

/* 2: (frames + 1) * DS_AUDIO_CLOCK / DS_AUDIO_RATE is where the mixer puts frame `frames` */
#undef DS_AUDIO_CLOCK
#undef DS_AUDIO_RATE
#define DS_AUDIO_CLOCK 1024u
#define DS_AUDIO_RATE 1u

/* 1: what a channel holds of its data, not yet played */
#define HELD 64u /* room for the 32 bytes and a burst more; a power of two */
#define BURST 16u

typedef struct Fetch {
    uint32_t source;       /* the channel's data when these were read */
    uint32_t next;         /* the offset in the data that the next burst starts at */
    uint32_t offset[HELD]; /* a ring of the bytes held, in the order they are played */
    uint8_t value[HELD];
    unsigned first, count;
} Fetch;

static Fetch fetch[16];
static DsAudioRead memory_read; /* the runtime's own reader, which fetched_read stands in front of */
static DsAudio *reading;        /* the channel that the mixer is reading for: set at each of its reads */
static unsigned reading_no;

/* Up to 16 more bytes of the channel's data, going back to the loop's start at the end. */
static void fetch_burst(Fetch *f, const DsAudioChannel *c, void *user)
{
    uint32_t total = (c->loop + c->length) * 4;
    if (f->next >= total) {
        if ((c->control >> 27 & 3) != 1 || c->length == 0) {
            return; /* played once: there is no more */
        }
        f->next = c->loop * 4;
    }
    uint32_t count = total - f->next < BURST ? total - f->next : BURST;
    for (uint32_t i = 0; i < count && f->count < HELD; i++) {
        unsigned at = (f->first + f->count) & (HELD - 1);
        f->offset[at] = f->next;
        f->value[at] = memory_read(user, f->source + f->next);
        f->next++;
        f->count++;
    }
}

/* A channel starts, or plays from somewhere it holds nothing of: two bursts from there. */
static void fetch_from(Fetch *f, const DsAudioChannel *c, void *user, uint32_t offset)
{
    f->source = c->source;
    f->first = f->count = 0;
    f->next = offset & ~3u;
    fetch_burst(f, c, user);
    fetch_burst(f, c, user);
}

static int fetch_find(const Fetch *f, uint32_t offset)
{
    for (unsigned i = 0; i < f->count; i++) {
        if (f->offset[(f->first + i) & (HELD - 1)] == offset) {
            return (int) i;
        }
    }
    return -1;
}

/* The mixer reads a byte of a channel's data: the byte as the channel fetched it. */
static uint8_t fetched_read(void *user, uint32_t address)
{
    const DsAudioChannel *c = &reading->channel[reading_no];
    Fetch *f = &fetch[reading_no];
    uint32_t offset = address - c->source;
    int at = f->source == c->source ? fetch_find(f, offset) : -1;
    if (at < 0) {
        fetch_from(f, c, user, offset);
        at = fetch_find(f, offset);
        if (at < 0) {
            return memory_read(user, address); /* outside the data it says it has */
        }
    }
    /* the bytes before it have been played; it stays itself, for ADPCM reads a byte twice */
    f->first = (f->first + (unsigned) at) & (HELD - 1);
    f->count -= (unsigned) at;
    uint8_t value = f->value[f->first];
    if (f->count - 1 <= BURST) {
        fetch_burst(f, c, user);
    }
    return value;
}

/* the decomp's functions that the ones below stand in front of */
#define ds_audio_init ds_audio_init_decomp
#define ds_audio_start ds_audio_start_decomp
#define ds_audio_timer ds_audio_timer_decomp
/* each of the mixer's reads says which channel it is for (all are made with `a` and `no`) */
#define read(user, address) read((reading = a, reading_no = no, (user)), (address))

void ds_audio_init_decomp(DsAudio *a, DsAudioRead read, DsAudioEmit emit, void *user);
void ds_audio_start_decomp(DsAudio *a, unsigned no);

#include "ds_audio.c"

#undef ds_audio_init
#undef ds_audio_start
#undef ds_audio_timer
#undef read

#include <stdio.h>
#include <stdlib.h>

/* PORT_SOUND_TRACE=FILE: a line for each channel's start (its kind, where its samples are,
 * the 12 bytes in front of them, which are a wave's header where it is one, and its period)
 * and for each change of a period, with the bus cycle: what the notes are played at. */
static FILE *trace_file;
static int trace_asked = -1;

static FILE *trace(void)
{
    if (trace_asked < 0) {
        const char *path = getenv("PORT_SOUND_TRACE");
        trace_asked = path != NULL && *path;
        trace_file = trace_asked ? fopen(path, "w") : NULL;
    }
    return trace_file;
}

void ds_audio_timer_decomp(DsAudio *a, unsigned no, uint32_t period);

void ds_audio_timer(DsAudio *a, unsigned no, uint32_t period)
{
    FILE *f = trace();
    if (f != NULL && no < 16 && a->channel[no].active && a->channel[no].period != 2 * ((period & 65535u) != 0 ? period & 65535u : 65536u)) {
        fprintf(f, "%llu timer %u %u\n", (unsigned long long) a->cycle, no, period & 65535u);
    }
    ds_audio_timer_decomp(a, no, period);
}

void ds_audio_init(DsAudio *a, DsAudioRead read, DsAudioEmit emit, void *user)
{
    ds_audio_init_decomp(a, read, emit, user);
    memory_read = read;
    memset(fetch, 0, sizeof(fetch));
    if (read != NULL) {
        a->read = fetched_read;
    }
}

void ds_audio_start(DsAudio *a, unsigned no)
{
    int starts = no < 16 && !a->channel[no].active;
    if (starts && memory_read != NULL && (a->channel[no].control >> 29 & 3) != 3) {
        /* the first 32 bytes are read now, not when the first sample is played */
        fetch_from(&fetch[no], &a->channel[no], a->user, 0);
    }
    FILE *f = trace();
    if (f != NULL && starts) {
        const DsAudioChannel *c = &a->channel[no];
        fprintf(f, "%llu start %u control %08x source %08x period %u header", (unsigned long long) a->cycle, no, c->control,
                c->source, c->period / 2);
        for (uint32_t i = 0; i < 12 && memory_read != NULL && (c->control >> 29 & 3) != 3; i++) {
            fprintf(f, " %02x", memory_read(a->user, c->source - 12 + i));
        }
        fprintf(f, "\n");
    }
    ds_audio_start_decomp(a, no);
}

/* After the console's reset (arm7_port.c) the mixer is made anew, and its time with it: it
 * goes on from the bus cycle the console is at, not from the start's. */
void ds_audio_port_resume(DsAudio *a, uint64_t cycle)
{
    a->cycle = cycle;
    a->frames = cycle * DS_AUDIO_RATE / DS_AUDIO_CLOCK;
}
