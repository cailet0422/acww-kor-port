#version 450
// The touch buttons: triangles in the window's pixels, each vertex with its colour.
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 v_color;

layout(push_constant) uniform Window {
    vec2 size;
} window;

void main()
{
    gl_Position = vec4(in_pos / window.size * 2.0 - 1.0, 0.0, 1.0);
    v_color = in_color;
}
