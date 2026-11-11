<p align="center"><b>English</b> · <a href="renderer.ko.md">한국어</a></p>

# The GPU renderer (plan)

Not written yet. This page is the plan for the roadmap's item 4: the 3D scene drawn by the
GPU at the phone's own resolution, with antialiasing, through Vulkan.

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

1. the scene handed out, and a second software rasteriser in the port that draws it at any
   whole multiple of the DS's size, checked against the decomp's at 1x: this proves the
   hand-over and is the reference for the shaders;
2. the three-answer mix of the 2D layers, checked the same way at 1x;
3. the Vulkan renderer, a feature at a time against that reference, on a software Vulkan
   driver here and then on phones;
4. the settings: resolution (automatic by default), antialiasing, and the software
   renderer kept as a choice.

## What it will not change

The game's own frames. The decomp's software renderer goes on producing the pictures the
frame checks compare; the GPU's picture is what is shown.
