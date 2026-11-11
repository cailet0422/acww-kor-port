<p align="center"><b>English</b> · <a href="renderer.ko.md">한국어</a></p>

# A larger 3D picture, and the GPU renderer (plan)

The roadmap's item 4 is the 3D scene drawn by the GPU at the phone's own resolution, with
antialiasing, through Vulkan. That renderer is not written yet. What there is, as of 0.6.0, is
the scene drawn at 2 or 3 times the DS's size by the DS's own rules, which is the picture the
GPU renderer will be checked against, and the mixing of a larger 3D picture with the game's
2D layers, which the GPU renderer will use as it is.

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

# The GPU renderer (plan)

## What there is now

The decomp's runtime draws everything itself at the DS's size: `gpu3d.c` (the geometry engine
and a rasteriser, 256 x 192) and `gpu2d.c` (the 2D engines, which take the 3D picture as one
of their layers and blend it with the others line by line). The front end enlarges the
finished picture. Enlarging cannot add detail: for sharp 3D the triangles have to be drawn
again at the larger size.

## What is needed

1. **The scene, out of the engine.** After the geometry engine has transformed, clipped and
   sorted a frame's polygons, they sit in a list (`PolygonList` in `gpu3d.c`): screen
   positions with depth and w, colours, texture coordinates, and each polygon's attributes,
   texture parameters and palette. A renderer of any kind starts from that list and the
   texture memory. The port already builds `gpu3d.c` through its own files
   (`port/src/gpu3d_main.c`, `gpu3d_upper.c`), so the list can be handed out without changing
   the decomp. Both halves of the tall view are lists of this kind.
2. **The DS's pixel rules, as shaders.** Seven texture formats (two with their own alpha,
   one compressed in 4 x 4 blocks), modulate, decal, toon and highlight shading, shadow
   polygons, alpha test and blending with the polygon-ID rule, fog, edge marking, depth by z
   or by w. Each has to give what the software rasteriser gives, which stays the reference:
   a frame can be drawn both ways and compared.
3. **The 2D layers over and under it.** The game's 2D layers are blended with the 3D picture
   by rules set per frame (priorities, windows, alpha, brightness), at the DS's size. For a
   large 3D picture the 2D engine's work has to be split into "what lies under the 3D
   layer" and "what lies over it, and how the two mix", so that the mix can be done at the
   large size. The plan is to ask the 2D engine for each line three times, with the 3D layer
   all black, all white and absent: from the three answers the mix for any 3D colour follows,
   without changing `gpu2d.c`.
4. **Vulkan.** A swapchain on the window SDL gives, the scene drawn into a multisampled
   target at the window's size, the 2D mix over it, and the touch buttons. OpenGL ES stays
   as the way without Vulkan. Antialiasing is the target's multisampling; resolution follows
   the window.

## Order

1. done another way: the larger picture above is the reference, made by the decomp's own
   rasteriser in tiles;
2. done: the three-answer mix of the 2D layers;
3. the scene handed out of the engine as a list of polygons, and the Vulkan renderer, a feature at a time against that reference, on a software Vulkan
   driver here and then on phones;
4. the settings: resolution (automatic by default), antialiasing, and the software
   renderer kept as a choice.

## What it will not change

The game's own frames. The decomp's software renderer goes on producing the pictures the
frame checks compare; the GPU's picture is what is shown.
