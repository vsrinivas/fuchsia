#version 450
#extension GL_ARB_separate_shader_objects : enable

// A simple ambient-only lighting pass.

#ifndef USE_ATTRIBUTE_UV
#error UV coordinates are required for texture-mapping.
#endif

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 inUV;

#define USE_PAPER_SHADER_POINT_LIGHT 1
#define USE_PAPER_SHADER_MESH_INSTANCE 1
#define USE_PAPER_SHADER_MESH_MATERIAL 1
#include "shaders/paper/common/use.glsl"

void main() {
  vec4 light_color = point_lights[PaperShaderPushConstants.light_index].color;
  outColor = model_color *
             light_color *
             texture(material_tex, inUV);
}
