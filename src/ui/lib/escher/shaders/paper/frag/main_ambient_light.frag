#version 450
#extension GL_ARB_separate_shader_objects : enable

// A simple ambient-only lighting pass.

layout(location = 0) out vec4 outColor;
#ifdef USE_ATTRIBUTE_UV
layout(location = 0) in vec2 inUV;
#endif

#define USE_PAPER_SHADER_SCENE_DATA 1
#define USE_PAPER_SHADER_MESH_INSTANCE 1
#define USE_PAPER_SHADER_MESH_MATERIAL 1
#include "shaders/paper/common/use.glsl"

void main() {
  outColor = model_color
#ifndef DISABLE_AMBIENT_LIGHT
      * vec4(ambient_light_color, 1)
#endif
#ifdef USE_ATTRIBUTE_UV
      * texture(material_tex, inUV);
#endif
  ;
}
