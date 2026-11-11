/* The front end's ARM7: the decomp's model as it is (runtime/arm7.c), and the one thing of the
 * console it leaves out, which the game needs for its Wi-Fi Connection settings: the reset.
 *
 * The settings are a program of their own in the game's ROM. The title's "Wi-Fi settings"
 * starts it by resetting the console with a word for the next start (OS_ResetSystem), and it
 * comes back to the game the same way. What the ARM9 does is its own code, read from the ROM's
 * ITCM (OSi_DoResetSystem at 0x01ffd994, OSi_ReloadRomData, OSi_DoBoot):
 *
 *   - it sends the OS's message 0x10 (PXI tag 0xc) and waits for the same message back;
 *   - with interrupts off it reads its program and the ARM7's from the card again;
 *   - it waits for the ARM7's SYNC output to be 1, answers 1, clears the two shared areas
 *     (0x027ffd80 and 0x027fff80, 0x80 bytes each), waits for the ARM7's output to leave 1,
 *     puts 0 out and jumps to its entry.
 *
 * So the ARM7's part is: the message back, 1 on its output until the ARM9 answers, another
 * value until the ARM9's output is 0 again (the areas are cleared by then), and a start as
 * after power on: what it keeps in the shared area written again, its devices quiet, the
 * start's handshake from the top. The firmware's memory and the save chip stay as they are.
 */
#define arm7_step arm7_step_decomp
#define arm7_init arm7_init_decomp

void arm7_step_decomp(void);
void arm7_init_decomp(const char *rom);

#include "arm7.c"

#undef arm7_step
#undef arm7_init

#include "sound.h"

#define OS_MESSAGE_RESET 0x10u

void wm_port_restart(uint8_t *nvram);                      /* wm_port.c */
void ds_audio_port_resume(DsAudio *audio, uint64_t cycle); /* ds_audio_port.c */

static struct {
    int stage;        /* 0 none; 1 our output 1, waiting for the ARM9's 1; 2 for its 0 */
    uint64_t seen;    /* the OS's messages taken before */
    unsigned resets;  /* done so far */
    char rom[1024];   /* the ROM's path, which the sequencer is started with again */
} port7;

static void (*reset_listener)(void);

/* The front end's word on a reset (it has things of its own to put back). */
void arm7_port_on_reset(void (*listener)(void))
{
    reset_listener = listener;
}

unsigned arm7_port_resets(void)
{
    return port7.resets;
}

/* The ARM7 as arm7_init leaves it, without what a reset keeps: the firmware's memory with the
 * settings written since, the save chip, and what the run was started with. */
static void restart(void)
{
    arm7.sync_value = 8;
    arm7.sync_delay = 0;
    arm7.lock_done = 0;
    arm7.nv_count = 0;
    arm7.nv_status = 0x02;
    arm7.tp_command = arm7.tp_param = arm7.tp_auto = arm7.tp_index = arm7.tp_frame = 0;
    arm7.snd_shared_work = 0;
    arm7.snd_finished = 0;
    arm7.card_command = 0;
    arm7.card_expect_address = 0;
    wr32(SHARED_FIFO_READY_ARM7, 0xfffffffeu);
    buttons_update();
    touch_sample();
    rtc_pin();
    rtc_update();
    if (arm7.wireless) {
        wm_port_restart(arm7.nvram);
        if (arm7.wifi) {
            wifi_disconnect();
        }
    }
    if (arm7.sequencer) {
        /* every channel off and the driver as at the start; the mixer goes on from now */
        arm7.sequencer = sequencer_init(port7.rom);
        DsAudio *mixer = sound_mixer();
        if (mixer != NULL) {
            ds_audio_port_resume(mixer, io_cycles());
        }
    }
    port7.resets++;
    if (reset_listener != NULL) {
        reset_listener();
    }
}

void arm7_step(void)
{
    if (port7.stage == 1) {
        io_ipc_set_arm7_out(1);
        if (io_ipc_arm9_out() == 1) {
            port7.stage = 2;
        }
        return;
    }
    if (port7.stage == 2) {
        io_ipc_set_arm7_out(0);
        if (io_ipc_arm9_out() == 0) {
            port7.stage = 0;
            restart();
        }
        return;
    }
    arm7_step_decomp();
    if (arm7.sent[0xc] != port7.seen) {
        port7.seen = arm7.sent[0xc];
        if ((arm7.last_data[0xc] >> 8 & 0x7f) == OS_MESSAGE_RESET) {
            /* counted as unanswered by the model's step; it is answered here */
            arm7.unanswered[0xc]--;
            reply(0xc, OS_MESSAGE_RESET << 8, 0);
            port7.stage = 1;
            io_ipc_set_arm7_out(1);
        }
    }
}

void arm7_init(const char *rom)
{
    port7.stage = 0;
    port7.seen = 0;
    snprintf(port7.rom, sizeof(port7.rom), "%s", rom != NULL ? rom : "");
    arm7_init_decomp(rom);
}
