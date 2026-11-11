#version 450
// A screen's rectangle in the window: four corners from the rectangle in the push constants.
layout(push_constant) uniform Screen {
    vec4 rect;       // in the window, as clip coordinates: left, top, right, bottom
    vec4 scene_uv;   // the scene's part for this screen: left, top, right, bottom
    vec4 bright;     // the 3D colour's master brightness: towards white, towards black, -, -
    vec2 texel_px;   // window pixels to one of the DS's pixels
    uint kind;
    uint pad;
} screen;

layout(location = 0) out vec2 v_uv;       // over the DS's screen, 0 to 1
layout(location = 1) out vec2 v_scene_uv;

void main()
{
    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    gl_Position = vec4(mix(screen.rect.xy, screen.rect.zw, corner), 0.0, 1.0);
    v_uv = corner;
    v_scene_uv = mix(screen.scene_uv.xy, screen.scene_uv.zw, corner);
}
