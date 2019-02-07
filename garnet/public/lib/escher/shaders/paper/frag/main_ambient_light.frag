#version 450
#extension GL_ARB_separate_shader_objects : enable

// A simple ambient-only lighting pass.

#ifndef USE_ATTRIBUTE_UV
#error UV coordinates are required for texture-mapping.
#endif

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 inUV;

#define USE_PAPER_SHADER_SCENE_DATA 1
#define USE_PAPER_SHADER_MESH_INSTANCE 1
#define USE_PAPER_SHADER_MESH_MATERIAL 1
#include "shaders/paper/common/use.glsl"

void main() {
  outColor = model_color *
             vec4(ambient_light_color, 1) *
             texture(material_tex, inUV);
}
