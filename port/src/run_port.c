/* The run: decomp/runtime/run.c as it is (built with RUN_FRONT_END, so the front end's main
 * calls run_main), with one thing added behind it: the port's hooks into the game's code
 * (game_hooks.h) are asked for the branch targets the run's own hooks leave alone. */
#include "run.c"

#include "game_hooks.h"

static ArmHook (*run_hook_at)(void *user, uint32_t address, void **hook_user);

/* the run was started with something that needs its whole look at every branch target */
static int whole_look;
/* Wi-Fi Connection is on (arm7.c: it does not change once the run has started) */
static int wifi_hooks;

/* Every branch target comes here, hundreds of thousands a frame. The run's own look at one
 * (hook_at) also counts the symbol it is in and watches for the SDK's thread switches, for
 * its report and for the Native form; the front end has neither (it runs the Reference form
 * and ends without the report). So once the game's code is loaded and checked, which the
 * run's look does, only what can answer is asked: the wireless shims when the wireless is on,
 * the BIOS, and the port's own hooks. The last branch targets are still kept for the run's
 * word on a crash. */
static ArmHook port_hook_at(void *user, uint32_t address, void **hook_user)
{
    ArmHook hook = NULL;
    if (__builtin_expect(whole_look || verified < 0, 0)) {
        hook = run_hook_at(user, address, hook_user);
    } else {
        ring[ring_count++ % RING] = address;
        /* nearly always nothing: the wireless shims only with the wireless on, the BIOS's
           hook outside the game's memory (its addresses begin 0x01 and 0x02), and the port's
           own at a few kinds of address */
        if (__builtin_expect(wifi_hooks, 0)) {
            hook = wifi_hook_at(address, hook_user);
        }
        if (hook == NULL && __builtin_expect(address >> 28 != 0, 0)) {
            hook = bios_hook_at(user, address, hook_user);
        }
    }
    if (hook != NULL) {
        return hook;
    }
    return (game_hook_kinds >> GAME_HOOK_KIND(address)) & 1 ? game_hook_at(address, hook_user) : NULL;
}

void port_hooks_install(void)
{
    if (cpu.hook_at != NULL && cpu.hook_at != port_hook_at) {
        const char *native = getenv("RUN_NATIVE");
        run_hook_at = cpu.hook_at;
        cpu.hook_at = port_hook_at;
        wifi_hooks = arm7_wifi();
        /* RUN_DRIVE, RUN_TRACE's calls, a driven run, or Native functions (none of them the
           front end's own doing): the run's look as it is */
        whole_look = driving || traced_count != 0 || driven_enabled() || native == NULL || strcmp(native, "0") != 0 ||
                     getenv("RUN_NATIVE_RECORD") != NULL || getenv("RUN_NATIVE_REPLAY") != NULL;
    }
}
