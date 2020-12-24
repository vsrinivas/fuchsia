#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_control_flow_attributes : enable

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
  outColor = model_color;

#ifndef DISABLE_AMBIENT_LIGHT
  outColor.rgb *= ambient_light_color;
#endif

#ifdef USE_ATTRIBUTE_UV
  vec4 material_color = texture(material_tex, inUV);

  [[dont_flatten]]
  if (gamma_power.x != 1.0) {
    // Gamma correction is required.  Check whether we can avoid pow().
    [[dont_flatten]]
    if (gamma_power.x == 2.0) {
      // Squaring is cheaper than pow().
      material_color.rgb *= material_color.rgb;
    } else {
      // pow() is required in the general case.
      material_color.rgb = pow(material_color.rgb, gamma_power.xxx);
    }
  }
  outColor *= material_color;
#endif
}
