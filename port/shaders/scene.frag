#version 450
// A pixel of a DS polygon, by the DS's rules (GBATEK, "DS 3D Texture Blending", "DS 3D
// Toon, Edge, Fog, Alpha-Blending"; decomp/runtime/gpu3d.c is the reference): the texture
// with the vertex colour by the polygon's mode, the alpha test, fog. The colour goes out
// multiplied by its alpha.
layout(location = 0) in vec3 v_color;
layout(location = 1) in vec2 v_st;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform Frame {
    vec4 toon[32];
    vec4 fog_color;      // rgb, and the fog's alpha
    vec4 fog_table[9];   // 34 densities, 0 to 1: the 32 with the first and last repeated
    float fog_offset;    // in 24-bit depth units
    float fog_scale;     // table steps per depth unit
    float alpha_ref;     // 0 to 31
    float pad;
} frame;

layout(set = 1, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform Polygon {
    vec2 inv_size;   // 1 / the texture's size
    float alpha;     // the polygon's alpha, 0 to 1
    uint flags;
} poly;

const uint TEXTURED = 1u, MODE = 6u, HIGHLIGHT = 8u, FOG = 16u, FOG_ALPHA_ONLY = 32u;
const uint ONLY_OPAQUE = 64u, ONLY_TRANSLUCENT = 128u, W_BUFFER = 256u, DEPTH_EQUAL = 512u;

float fog_entry(int i)
{
    return frame.fog_table[i >> 2][i & 3];
}

void main()
{
    uint mode = (poly.flags & MODE) >> 1;
    vec3 v = v_color;
    int toon_at = int(clamp(v_color.r, 0.0, 1.0) * 31.0 + 0.5);
    bool highlight = mode == 2u && (poly.flags & HIGHLIGHT) != 0u;
    if (mode == 2u) {
        v = highlight ? vec3(v_color.r) : frame.toon[toon_at].rgb;
    }
    vec3 rgb = v;
    float a = poly.alpha;
    if ((poly.flags & TEXTURED) != 0u) {
        vec4 t = texture(tex, v_st * poly.inv_size);
        if ((mode & 1u) != 0u) { // decal, and shadow
            rgb = mix(v, t.rgb, t.a);
        } else {                 // modulation
            rgb = t.rgb * v;
            a = t.a * poly.alpha;
        }
    }
    if (highlight) {
        rgb = min(rgb + frame.toon[toon_at].rgb, vec3(1.0));
    }
    float a31 = floor(a * 31.0 + 0.5);
    if (a31 <= frame.alpha_ref) {
        discard;
    }
    bool opaque = a31 >= 31.0;
    if ((opaque && (poly.flags & ONLY_TRANSLUCENT) != 0u) || (!opaque && (poly.flags & ONLY_OPAQUE) != 0u)) {
        discard;
    }
    a = opaque ? 1.0 : a31 / 31.0;
    float depth = gl_FragCoord.z;
    if ((poly.flags & W_BUFFER) != 0u) {
        depth = clamp(4096.0 / (gl_FragCoord.w * 16777215.0), 0.0, 1.0);
        // a polygon that passes on "equal" depth (a decal on its surface): the DS takes w
        // within 0xff of 24 bits for equal; here it is brought that much nearer
        gl_FragDepth = (poly.flags & DEPTH_EQUAL) != 0u ? depth - 256.0 / 16777215.0 : depth;
    }
    if ((poly.flags & FOG) != 0u) {
        float at = max(depth * 16777215.0 - frame.fog_offset, 0.0) * frame.fog_scale;
        int i = int(min(floor(at), 32.0));
        float f = i >= 32 ? 0.0 : fract(at);
        float d = mix(fog_entry(i), fog_entry(i + 1), f);
        d = d >= 127.0 / 128.0 ? 1.0 : d;
        if ((poly.flags & FOG_ALPHA_ONLY) == 0u) {
            rgb = mix(rgb, frame.fog_color.rgb, d);
        }
        a = mix(a, frame.fog_color.a, d);
    }
    out_color = vec4(rgb * a, a);
}
