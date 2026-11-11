#version 450
// One of the DS's screens with the large 3D scene in it (docs/renderer.md).
//
// The scene is the 3D layer at the window's size: colour multiplied by alpha, and alpha. The
// game's 2D layers are at the DS's size. Kinds of screen:
//   0  the screen as the game drew it (no scene to show);
//   1  the scene over the screen as the game drew it (the upper screen of the tall view);
//   2  the screen the 3D layer belongs to. The 2D engine has drawn it three times, with a 3D
//      layer all black (B), all white (W) and absent (N): the pixel for a 3D colour c with
//      alpha a is N (1 - a) + (B + (W - B) c) a, which with the colour already multiplied by
//      its alpha is N (1 - a) + B a + (W - B) C.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_scene_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D frame;   // the screen as the game drew it
layout(set = 0, binding = 2) uniform sampler2D black;
layout(set = 0, binding = 3) uniform sampler2D white;
layout(set = 0, binding = 4) uniform sampler2D none;

layout(push_constant) uniform Screen {
    vec4 rect;
    vec4 scene_uv;
    vec4 bright;
    vec2 texel_px;
    uint kind;
    uint pad;
} screen;

const vec2 SIZE = vec2(256.0, 192.0);

// The DS's pixels enlarged by a fraction: each keeps its square, with a pixel of the window
// between two of them mixed (the texture is read with a linear filter at a point moved
// towards the texel's middle).
vec2 sharp(vec2 uv)
{
    vec2 p = uv * SIZE, middle = floor(p) + 0.5;
    return (middle + clamp((p - middle) * screen.texel_px, -0.5, 0.5)) / SIZE;
}

void main()
{
    vec2 uv = sharp(v_uv);
    if (screen.kind == 0u) {
        out_color = vec4(texture(frame, uv).rgb, 1.0);
        return;
    }
    vec4 s = texture(scene, v_scene_uv);
    if (screen.kind == 1u) {
        // the main engine's master brightness, which the upper screen's own picture does not have
        vec3 c = s.rgb + (vec3(s.a) - s.rgb) * screen.bright.x;
        c *= 1.0 - screen.bright.y;
        out_color = vec4(c + texture(frame, uv).rgb * (1.0 - s.a), 1.0);
        return;
    }
    vec3 b = texture(black, uv).rgb, w = texture(white, uv).rgb, n = texture(none, uv).rgb;
    out_color = vec4(clamp(n * (1.0 - s.a) + b * s.a + (w - b) * s.rgb, 0.0, 1.0), 1.0);
}
