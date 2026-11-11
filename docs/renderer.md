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

`--vulkan` (`vulkan=1` in the settings, "Vulkan renderer" on Android's settings screen).
Experimental and off by default: it has run on a software Vulkan driver, not yet on a phone.

With it the window is drawn through Vulkan: the 3D scene at the size its screens have in the
window, four samples a pixel, the game's 2D layers mixed with it, and the touch buttons over
them. Where there is no Vulkan, the window opens as ever and the setting turns itself off.

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
   buffer a frame; a new surface when the app comes back to the screen. Vulkan is not linked:
   its functions are looked up when the program runs, so the same build runs where there is
   none.

The game's own frames are not touched: the main engine's software picture still goes into
them, and the frame checks pass with Vulkan on.

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

## Not done

- A phone: nothing here has run on one yet.
- Shadow volumes, edge marking, wire frames and the rear plane's bitmap, none of which the
  scenes looked at use. The DS's own anti-aliasing is what multisampling is in place of.
- Settings for the number of samples and for a size below the window's, for slower devices.
- OpenGL ES as the way without Vulkan draws the software picture, as before.
