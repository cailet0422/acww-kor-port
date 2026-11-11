#!/bin/sh
# port/shaders/*.vert and *.frag compiled to SPIR-V and written as C arrays into
# port/src/vk_shaders.h, which is kept in the repository so that a build needs no shader
# compiler. Run it after changing a shader (needs glslangValidator: glslang-tools).
set -e
cd "$(dirname "$0")/.."
out=src/vk_shaders.h
tmp=$(mktemp -d)
{
    echo "/* The shaders of port/shaders, compiled (port/tools/make_shaders.sh writes this file). */"
    echo "#ifndef ACWW_PORT_VK_SHADERS_H"
    echo "#define ACWW_PORT_VK_SHADERS_H"
    echo "#include <stdint.h>"
    for f in shaders/*.vert shaders/*.frag; do
        name=$(basename "$f" | tr . _)
        glslangValidator -V --quiet "$f" --vn "shader_$name" -o "$tmp/$name.h" >/dev/null
        grep -v "^[[:space:]]*\(//\|#pragma\)" "$tmp/$name.h" | grep -v "^$" | sed 's/^const uint32_t/static const uint32_t/'
    done
    echo "#endif"
} > "$out"
rm -rf "$tmp"
echo "wrote $out"
