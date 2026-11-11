<p align="center"><b>English</b> · <a href="roadmap.ko.md">한국어</a></p>

# Roadmap

What the port does now and the order the rest goes in. The facts about the decomp are from
`decomp/` at the pinned commit (`docs/runtime.md`, `docs/play.md` and the runtime's sources).

## Where it stands (0.6.1)

| | State |
|---|---|
| The game in the port | runs; the decomp's recordings give the decomp's frames, bit for bit, on x86 and ARM, 32- and 64-bit ([64bit.md](64bit.md)) |
| Android APK | `armeabi-v7a` and `arm64-v8a`, built by CI; runs on a phone |
| Linux | x86-64 and 32-bit x86, built by CI |
| ROM check and split | the same modules as the decomp's `tools/extract.py`, byte for byte (`port/tests/test_rom_prep.py`) |
| Speed | the title recording runs at 3 to 4 times the DS's speed on a server CPU, the drawing on a thread of its own; on phones, to be measured (the settings screen shows it) |
| Sound | on: the runtime's mixer, with the DS's data fetching so that the game's surround is heard, through an SDL audio device ([sound.md](sound.md)) |

## What the decomp's runtime assumes

1. **DS addresses are host pointers.** The DS's memory is mapped at its own addresses, and
   `runtime/memory.c` builds only for 32 bits. The Reference form works in a 64-bit process
   all the same; the port builds it there and checks its frames ([64bit.md](64bit.md)).
2. **x86 for the compiled forms.** The Native and Clean forms call compiled functions with x86
   conventions (`runtime/host.c`), catch calls into DS code through x86 fault frames
   (`runtime/native_os.c`), patch x86 jumps into code and enter the interpreter through x86
   assembly (`runtime/native_tramp.S`). The port runs the Reference form and replaces those
   files with stand-ins.
3. **The interpreter's speed-up is x86 code** (`runtime/arm_x86.c`, 32-bit x86 only). On ARM
   every instruction is interpreted.
4. **Win32 for the window and sound.** `runtime/play.c` and the device part of
   `runtime/sound.c` (`waveOut`); the mixer itself is portable.
5. **A software renderer at the DS's size.** Both screens are drawn by the runtime at
   256 x 192 (`runtime/gpu2d.c`, `runtime/gpu3d.c`).
6. **Wireless between its own consoles only.** The runtime simulates the DS's wireless manager
   for runs of two consoles side by side on one PC, exchanging frames through pipes, and Wi-Fi
   Connection against servers simulated inside the process (`docs/multi-console.md`). None of
   it reaches a network, and the window (`play.c`) plays one console alone.

## The plan, in order

1. **64-bit phones.** Done in 0.2.0, as far as it can be checked without a phone. Next: run it
   on real phones, measure the speed, fix what breaks.
2. **A phone's way of playing.**
   - portrait first: done. The picture takes the window's whole width, the buttons the room
     below it, or lie over its lower part where there is none (tablets, foldables);
   - a full-touch mode, the game played with the stylus alone and no buttons drawn: done. The
     mark with three dots (the settings screen on Android) turns the buttons on and off,
     and the choice is kept;
   - the buttons where the player wants them: done. Each can be dragged and resized, for
     tall and for wide screens apart;
   - a settings screen on Android, in Korean by default: done. The mark with three dots
     opens it;
   - the two screens as one picture: done for the 3D scene ([tall-view.md](tall-view.md));
   - **widening what the game draws above its own screen.** The game leaves out what is off
     the lower screen, so the upper part of the tall view lacks whatever lies wholly above it.
     Done for the game's objects (actors), by a hook where the game asks whether to draw one
     ([tall-view.md](tall-view.md)): the simulation is untouched and the recordings give the
     decomp's frames. Still to do: the ground and what stands on it (trees, buildings), which
     other code draws; find it in the decomp, see whether it leaves anything out above the
     screen, and widen it the same way, checked against the recordings;
   - cutouts and rounded corners on real phones: to be checked on devices;
   - Mr. Resetti on or off: done, off by default (`resetti=1` in the settings, or `--resetti`,
     brings him back). When a town is loaded the game asks whether the last play ended
     without a save; with him off the answer is no (`port/src/game_hooks.c`). Played
     through both ways ([autosave.md](autosave.md));
   - autosave: done, on by default ([autosave.md](autosave.md)). The town's state block is
     written as the game's save, with the game's sums, to a file beside the save, every
     minute and when the app goes out of sight, at moments the game would let the player
     save; the next start asks which to go on from. To check: a town played for days.
3. **Sound** ([sound.md](sound.md)). Done: the decomp's sequencer and mixer, the mixer with
   the DS's fetching of data ahead (without it the game's surround is never heard) and the
   DS's output rate; converted to the device's rate and kept in step with its clock
   (`port/src/ds_audio_port.c`, `sound_sdl.c`). To do: a volume setting.
4. **A Vulkan renderer** ([renderer.md](renderer.md)). Done, as an experimental option
   (`--vulkan`): the 3D scene handed out of the engine as polygons and drawn by the GPU at
   the window's own size, four samples a pixel, by the DS's rules; the 2D layers mixed with
   it and the buttons drawn over; the software rasteriser stays the reference, and the game's
   frames are untouched. Checked on a software Vulkan driver. To do: a phone; the settings
   for samples and for a smaller size; the DS features Wild World's scenes have not needed.
5. **Playing together.**
   - port players with each other: the decomp's simulated local wireless carried over the
     network instead of pipes between processes;
   - Nintendo Wi-Fi Connection through the game's own network code: the decomp's simulated
     access point opened to the real network, with the server chosen by DNS, so a private
     server works as it does on a console, and port and console players meet on the same server;
   - a server of the project's own for port players;
   - the console's settings the game reads (the user's name, birthday, the Wi-Fi connection's
     identity) are made by the port from scratch: no firmware or NAND image is used or read.
   - DS-to-DS local wireless with a real console is out of reach: a phone cannot send or receive
     the DS's radio protocol.
6. **Speed, where a phone needs it.** Profiling first; then an ARM block translator
   (`arm_x86.c` shows the shape), or the compiled forms on ARM.
7. **Upstream.** Small changes to the decomp so its runtime builds on other hosts as it is,
   and the port's stand-ins shrink.
