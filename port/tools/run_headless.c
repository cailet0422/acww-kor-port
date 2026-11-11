/* acww-run: the decomp's headless program, run.c with its own main (no RUN_FRONT_END):
 *
 *     acww-run <rom.nds> <modules.txt> <symbols.txt> [max_instructions] [max_frames]
 *
 * configured by the environment as decomp/tools/run.py does (RUN_INPUT, RUN_RTC, RUN_HASH,
 * ...). port/tests/test_frames.py plays the decomp's recordings with it and compares the
 * frames with the decomp's expected ones, which is how a new host (64-bit, ARM) is checked. */
#include "../../decomp/runtime/run.c"
