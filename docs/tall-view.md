<p align="center"><b>English</b> · <a href="tall-view.ko.md">한국어</a></p>

# The tall view

The game draws its 3D scene on the lower screen only; the upper screen is a sky outdoors and
black indoors. The port continues the scene onto the upper screen, so the two screens show one
picture: indoors the walls and the ceiling, outdoors the roofs and treetops against the sky.
It is on by default; `--two-screens` gives the DS's two pictures as they are.

## How

Nothing in the game or in the decomp's 3D engine is changed. The engine
(`decomp/runtime/gpu3d.c`) is built again under other names (`port/src/gpu3d_tile.c`), and
`port/src/tall3d.c` stands in front of both (the same copies, more of them, draw the scene at
a larger size: [renderer.md](renderer.md)):

- every 3D command the game writes goes to the main engine as it is, and to the second engine
  too, except that a projection matrix the game loads is first moved up by one screen
  (`y' = y - 2w` in clip coordinates). What was just above the lower screen's top edge is
  inside the second engine's picture;
- the second engine has a thread of its own: it plays a frame's commands back when the frame
  is complete, and draws when the drawing thread comes to that frame, from the same textures
  and registers, with a transparent rear plane;
- the front end lays its picture over the upper screen (`lay_tall` in `port/src/front_sdl.c`):
  opaque where the scene is, blended at translucent pixels and antialiased edges, with the
  main screen's brightness, so fades go together.

The frames the runtime produces are untouched: `port/tests/test_frames.py` gives the decomp's
sequences with the split engine as before. The headless programs never start the second
engine.

## Objects above the game's screen

The game checks each of its objects against the camera's frustum once a frame and marks it
"far" when it is outside or deeper than the object's own limit; a class may skip running and
drawing its far objects (`Actor_CanRun`, `Actor_CanDraw` in `decomp/clean/main/actor.c`). An
object above the lower screen's top edge is outside, so the game would not draw it, and the
upper part of the tall view would lack it.

`port/src/game_hooks.c` hooks the entry of `Actor_CanDraw` in the interpreter. For an object
marked far, it makes the check again with the frustum's top plane raised by two screens,
from what the game's own check left in the object; if the object is then inside, the mark is
cleared for the length of that one call and put back at its return. Only the answer to
"draw?" changes. Whether an object runs, which is the game's simulation, is decided as
before: with the hook on, the new-game recording gives the decomp's frames for both screens,
all 13,760 of them, while 33 drawings were added. `--no-above` turns it off.

## Limits

- **What the game does not send is not there.** The game leaves out what is off its own
  screen. For its objects (actors) the port widens that, below; for anything the game leaves
  out by other means (the ground and what stands on it are drawn by other code, not yet
  looked into), tall things that start on the lower screen continue, and something wholly
  above it may be missing.
- **The upper screen's own pictures are under the scene.** Where the game shows something of
  its own on the upper screen while the 3D scene is on the lower one, the scene is drawn over
  it wherever it has something to draw.
- **The round wipe between scenes** hides the upper part for its duration (the 3D layer's
  windows are not followed onto the upper screen).
- **Cost.** The scene's geometry and drawing are done twice: about an eighth slower in the
  runs measured.
