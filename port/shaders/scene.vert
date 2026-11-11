#version 450
// A vertex of the DS's scene (port/src/scene.h): clip coordinates as the DS has them.
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec2 in_st;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec2 v_st;

void main()
{
    // the DS: y up, z from -w (near) to w (far). Vulkan: y down, z from 0 to w.
    gl_Position = vec4(in_pos.x, -in_pos.y, (in_pos.z + in_pos.w) * 0.5, in_pos.w);
    v_color = in_color;
    v_st = in_st;
}
