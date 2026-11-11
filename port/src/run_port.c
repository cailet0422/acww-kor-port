/* The run: decomp/runtime/run.c as it is (built with RUN_FRONT_END, so the front end's main
 * calls run_main), with one thing added behind it: the port's hooks into the game's code
 * (game_hooks.h) are asked for the branch targets the run's own hooks leave alone. */
#include "run.c"

#include "game_hooks.h"

static ArmHook (*run_hook_at)(void *user, uint32_t address, void **hook_user);

/* the run was started with something that needs its whole look at every branch target */
static int whole_look;

/* Every branch target comes here, hundreds of thousands a frame. The run's own look at one
 * (hook_at) also counts the symbol it is in and watches for the SDK's thread switches, for
 * its report and for the Native form; the front end has neither (it runs the Reference form
 * and ends without the report). So once the game's code is loaded and checked, which the
 * run's look does, only what can answer is asked: the wireless shims when the wireless is on,
 * the BIOS, and the port's own hooks. The last branch targets are still kept for the run's
 * word on a crash. */
static ArmHook port_hook_at(void *user, uint32_t address, void **hook_user)
{
    ArmHook hook;
    if (whole_look || verified < 0) {
        hook = run_hook_at(user, address, hook_user);
    } else {
        ring[ring_count++ % RING] = address;
        hook = arm7_wifi() ? wifi_hook_at(address, hook_user) : NULL;
        if (hook == NULL) {
            hook = bios_hook_at(user, address, hook_user);
        }
    }
    return hook != NULL ? hook : game_hook_at(address, hook_user);
}

void port_hooks_install(void)
{
    if (cpu.hook_at != NULL && cpu.hook_at != port_hook_at) {
        const char *native = getenv("RUN_NATIVE");
        run_hook_at = cpu.hook_at;
        cpu.hook_at = port_hook_at;
        /* RUN_DRIVE, RUN_TRACE's calls, a driven run, or Native functions (none of them the
           front end's own doing): the run's look as it is */
        whole_look = driving || traced_count != 0 || driven_enabled() || native == NULL || strcmp(native, "0") != 0 ||
                     getenv("RUN_NATIVE_RECORD") != NULL || getenv("RUN_NATIVE_REPLAY") != NULL;
    }
}
