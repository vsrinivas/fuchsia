#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 outColor;

// For shadow map generation passes, main() encodes the depth into outColor.
// The encoding scheme differs depending on the specific shadow map technique.
#ifdef SHADOW_MAP_GENERATION_PASS
#ifdef SHADOW_MAP_LIGHTING_PASS
#error SHADOW_MAP_GENERATION_PASS conflicts with SHADOW_MAP_LIGHTING_PASS
#endif
#ifdef NO_SHADOW_LIGHTING_PASS
#error SHADOW_MAP_GENERATION_PASS conflicts with NO_SHADOW_LIGHTING_PASS
#endif
#include "shaders/model_renderer/shadow_map_generation.frag"
#endif  // SHADOW_MAP_GENERATION_PASS

#ifdef SHADOW_MAP_LIGHTING_PASS
#ifdef NO_SHADOW_LIGHTING_PASS
#error SHADOW_MAP_LIGHTING_PASS conflicts with NO_SHADOW_LIGHTING_PASS
#endif
#include "shaders/model_renderer/shadow_map_lighting.frag"
#endif  // SHADOW_MAP_LIGHTING_PASS

#ifdef NO_SHADOW_LIGHTING_PASS

layout(location = 0) in vec2 inUV;
layout(set = 0, binding = 0) uniform PerModel {
  vec2 frag_coord_to_uv_multiplier;
  float time;
};
layout(set = 1, binding = 0) uniform PerObject {
  mat4 camera_transform;
  vec4 color;
};
layout(set = 1, binding = 1) uniform sampler2D material_tex;

void main() {
  outColor = color * texture(material_tex, inUV);
}

#endif  // NO_SHADOW_LIGHTING_PASS
