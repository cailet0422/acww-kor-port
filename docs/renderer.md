<p align="center"><b>English</b> · <a href="renderer.ko.md">한국어</a></p>

# A larger 3D picture, and the GPU renderer

The roadmap's item 4 is the 3D scene drawn by the GPU at the phone's own resolution, with
antialiasing, through Vulkan. There are two things here: the scene drawn at 2 or 3 times the
DS's size by the DS's own rules, in software, which is the picture the GPU renderer is
checked against, with the mixing of a larger 3D picture with the game's 2D layers; and,
since 0.8.0, the GPU renderer itself (below), which uses that mixing as it is.

## The larger picture there is now

`--resolution 2` (or 3; `resolution=` in the settings, "3D resolution" on Android's settings
screen). Off by default: it is heavy.

- **The scene, in tiles** (`port/src/tall3d.c`). The view is cut into tiles of the DS's size,
  `scale` x `scale` to a screen. Each tile has a copy of the decomp's 3D engine
  (`gpu3d_tile.c`, the engine unchanged) that gets the game's commands with the projection
  zoomed onto its tile, and draws it as its whole screen. Side by side the tiles are the
  scene at `scale` times the size, every pixel made by the engine's own rules: textures,
  shading, edges, fog. Each tile has a thread: it plays the frame's commands into its engine
  when the frame is complete and draws when the drawing thread comes to that frame, so the
  tiles work on as many cores as there are.
- **The 2D layers mixed in** (`port/src/mix2d.c`). Three more copies of the 2D engine draw
  each line with a 3D layer that is all black, all white and absent; from the three answers
  the front end works out each pixel of the large picture for the colour the large 3D scene
  has there (`put_together` in `front_sdl.c`). The game's dialogs, icons and fades lie over
  the large scene as they do over the small one.

Checked: at size 1 the picture is the same file as before; at 2 and 3 the runtime's own
frames are still the decomp's (the copies only read); the threaded tiles give the same
pictures as tiles drawn one after another.

Cost, on the 4-core machine this was written on, the new-game recording unpaced: 1x about
130 frames a second, 2x about 55 (35 before the tiles had threads). A phone will be slower.
That is why this is not the way to a phone's resolution, only the reference for it.

# The GPU renderer

On by default (`vulkan=0` in the settings, or `--no-vulkan`, turns it off; "Vulkan renderer"
on Android's settings screen).

With it the window is drawn through Vulkan: the 3D scene at the size its screens have in the
window, four samples a pixel, the game's 2D layers mixed with it, and the touch buttons over
them. `vulkan_scale` (100, 75 or 50; "Vulkan 3D resolution") draws the scene at less than
the window's size, for a GPU that is not fast enough; it takes effect at once.

Where the device has no Vulkan, or not what the renderer needs of it, the game starts
without it, with the picture drawn in software as before: it is found out at each start,
and the setting stays as it is. A start through Vulkan that never gets as far as showing
frames (a driver that crashes takes the app with it) is counted; after three in a row the
setting is turned off, and the settings screen says why. Nothing else turns it off.

## How

1. **The scene, out of the engine** (`port/src/scene.h`, `gpu3d_tile.c`). One copy of the
   decomp's 3D engine gets the game's commands with the projection changed so that the whole
   view, two screens high with the tall view, is its screen. It does not draw. When a frame's
   list is complete it hands it out: the polygons, transformed, lit, clipped and in drawing
   order, with clip coordinates; the textures they use, decoded by the engine's own reader
   and kept while their source bytes stay the same; and the registers the drawing goes by.
2. **The DS's pixel rules on a GPU** (`port/src/vk_scene.c`, `port/shaders/scene.frag`).
   Each polygon is a fan of triangles. Nearest texture sampling with the DS's repeating,
   flipping and clamping; modulation, decal, toon and highlight shading; the alpha test; fog
   on colour and alpha or on alpha only; depth by w, as Wild World has it, or by z. An opaque
   pixel is written with its depth; a translucent one is blended, writes its depth only if
   the polygon says so, and is not drawn over a translucent pixel of the same polygon ID
   (the stencil holds the ID a translucent pixel was last written with). The picture keeps
   colour multiplied by alpha, and alpha, which is what the mixing with the 2D layers needs
   and what makes multisampling right at the scene's outline.
3. **The 2D layers** (`port/src/mix2d.c`, `port/shaders/compose.frag`). The three answers of
   the 2D engine (above) give the lower screen's pixel for any 3D colour; over the upper
   screen the scene lies on the picture the game drew there. Both at the window's size, the
   DS's 2D pixels kept square.
4. **The window** (`port/src/vk_present.c`). A swapchain on SDL's Vulkan surface; one command
   buffer a frame. Vulkan is not linked: its functions are looked up when the program runs,
   so the same build runs where there is none.
5. **The surface's coming and going.** Android takes the window from under the surface
   whenever another screen comes to the front (the settings, another app). The surface and
   its swapchain are let go of before that happens, and a new surface is made when the app
   is back; the same is done after any frame that fails, since the system does not always
   say what was lost. If new surfaces keep failing, everything of Vulkan's is made anew
   once, and after that the window goes on without Vulkan for the rest of the run, with the
   software picture (the tall view included); the next start tries Vulkan again. What failed,
   with Vulkan's number for it, is in the log and on the settings screen's last page.
6. **A thread for the frames.** Drawing a frame waits for the GPU to be done with the one
   before, so it is not done on the emulation's thread: that one says what there is to draw
   and goes on. A GPU slower than the game leaves frames out; nothing queues up.

## What is left out while the GPU draws

What the software engines make for the same frame is not all needed then (`tall3d_lean`,
`gpu2d_main.c`):

- the main engine's own 3D picture is not drawn while the lower screen shows the scene and
  the game is not capturing the display (a capture reads the engine's lines; for a second
  after one, the picture is drawn again);
- the main 2D engine's lines with the 3D layer in them are not composed: the three answers
  are all that is taken of them;
- the answers of a line are kept from the frame before while everything they are made from
  is the same (a number made of the line's layers, windows and registers says so). In play
  about 95 lines in 100 are kept; `PORT_MIX_CHECK=1` makes every kept line again and
  compares.

So the game's own frames are then not the decomp's where the 3D layer is. A run that
records or compares frames (`--hash`, `--picture-hash`, `--shot`, `--scene-dump`) draws
everything, and the frame checks pass with Vulkan on.

## Checks

- `acww-kor --scene-dump --shot N` writes a frame's scene; `acww-scene-vk` draws it on this
  machine's Vulkan device; `port/tests/compare_scene.py` compares it with the software
  tiles' picture of the same frame (`--resolution 2 --shot N`). On five frames of the
  new-game recording on llvmpipe: colour 0.011 to 0.016 apart on average (of 1), the
  differences along the polygons' edges, where the DS places vertices on whole pixels.
- Under SDL's dummy video driver the window is a Vulkan surface without a display
  (`VK_EXT_headless_surface`): the whole path, swapchain included, runs in the checks, and
  `--window-shot` reads the picture back. The new-game recording was run through it with
  the validation layers on, without a complaint.
- `--vulkan-lose N` lets go of the surface at frame N as an app's leaving the screen does:
  the new surface, everything made anew (`PORT_VULKAN_BREAK=1`: no new surface at first)
  and the going on without Vulkan (`PORT_VULKAN_BREAK=2`) were each run, the first under
  the validation layers and the address sanitizer.
- On a phone: the renderer has been run on one by a player; what is described here was
  made after their report and has not been on a device yet.

## Not done

- Shadow volumes, edge marking, wire frames and the rear plane's bitmap, none of which the
  scenes looked at use. The DS's own anti-aliasing is what multisampling is in place of.
- A setting for the number of samples.
- OpenGL ES as the way without Vulkan draws the software picture, as before.
