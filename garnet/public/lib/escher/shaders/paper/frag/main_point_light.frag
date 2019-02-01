#version 450
#extension GL_ARB_separate_shader_objects : enable

// A simple ambient-only lighting pass.

#ifndef USE_ATTRIBUTE_UV
#error UV coordinates are required for texture-mapping.
#endif

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 inUV;

#ifdef USE_PAPER_SHADER_POINT_LIGHT_FALLOFF
layout(location = 1) in vec4 inIrradiance;
#endif

#define USE_PAPER_SHADER_POINT_LIGHT 1
#define USE_PAPER_SHADER_MESH_INSTANCE 1
#define USE_PAPER_SHADER_MESH_MATERIAL 1
#include "shaders/paper/common/use.glsl"

void main() {
  #ifndef USE_PAPER_SHADER_POINT_LIGHT_FALLOFF
    // If there is no light falloff, use the light's color/intensity as the
    // irradiance (if there is falloff, we interpolate a value computed by the
    // vertex shader).
    vec4 inIrradiance =
        point_lights[PaperShaderPushConstants.light_index].color;
  #endif

  outColor = inIrradiance *
             model_color *
             texture(material_tex, inUV);
}
