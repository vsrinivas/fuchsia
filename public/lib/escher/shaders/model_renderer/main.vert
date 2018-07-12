#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
  vec4 gl_Position;
};

layout(set = 0, binding = 0) uniform PerModel {
  vec2 frag_coord_to_uv_multiplier;
  float time;
  vec3 ambient_light_intensity;
  vec3 direct_light_intensity;
};

// Use binding 2 to avoid potential collision with PerModelSampler
layout(set = 0, binding = 2) uniform ViewProjection {
  mat4 vp_matrix;
};

// Attribute locations must match constants in model_data.h
layout(location = 0) in vec3 inPosition;
#ifdef USE_POSITION_OFFSET_ATTRIBUTE
layout(location = 1) in vec3 inPositionOffset;
#endif
#ifdef USE_UV_ATTRIBUTE
layout(location = 2) in vec2 inUV;
#endif
#ifdef USE_PERIMETER_ATTRIBUTE
layout(location = 3) in vec2 inPerimeter;
#endif

// Defines ComputeVertexPosition(), which returns the model-space position of
// the vertex.
#ifdef WOBBLE_VERTEX_POSITION
#include "shaders/model_renderer/wobble_position.vert"
#else
#include "shaders/model_renderer/default_position.vert"
#endif

#ifdef SHADOW_MAP_LIGHTING_PASS
layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 shadowPos;
void main() {
  vec4 pos = ComputeVertexPosition();
  gl_Position = vp_matrix * model_transform * pos;
  shadowPos = light_transform * pos;
  fragUV = inUV;
}
#endif

#ifdef NO_SHADOW_LIGHTING_PASS
layout(location = 0) out vec2 fragUV;
void main() {
  vec4 pos = ComputeVertexPosition();
  gl_Position = vp_matrix * model_transform * pos;
  fragUV = inUV;
}
#endif

#ifdef DEPTH_PASS
void main() {
  gl_Position = vp_matrix * model_transform * ComputeVertexPosition();
}
#endif
