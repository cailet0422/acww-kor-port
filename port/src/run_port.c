/* The run: decomp/runtime/run.c as it is (built with RUN_FRONT_END, so the front end's main
 * calls run_main), with one thing added behind it: the port's hooks into the game's code
 * (game_hooks.h) are asked for the branch targets the run's own hooks leave alone. */
#include "run.c"

#include "game_hooks.h"

static ArmHook (*run_hook_at)(void *user, uint32_t address, void **hook_user);

static ArmHook port_hook_at(void *user, uint32_t address, void **hook_user)
{
    ArmHook hook = run_hook_at(user, address, hook_user);
    return hook != NULL ? hook : game_hook_at(address, hook_user);
}

void port_hooks_install(void)
{
    if (cpu.hook_at != NULL && cpu.hook_at != port_hook_at) {
        run_hook_at = cpu.hook_at;
        cpu.hook_at = port_hook_at;
    }
}
