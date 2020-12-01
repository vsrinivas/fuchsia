#version 450
#extension GL_ARB_separate_shader_objects : enable

// A simple ambient-only lighting pass.

#ifndef USE_ATTRIBUTE_UV
#error UV coordinates are required for texture-mapping.
#endif

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inLightIntensity;

#define USE_PAPER_SHADER_POINT_LIGHT 1
#define USE_PAPER_SHADER_MESH_INSTANCE 1
#define USE_PAPER_SHADER_MESH_MATERIAL 1
#include "shaders/paper/common/use.glsl"

void main() {
  outColor = inLightIntensity *
             model_color *
             texture(material_tex, inUV);
}
