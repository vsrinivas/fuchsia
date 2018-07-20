#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
  vec4 gl_Position;
  float gl_ClipDistance[NUM_CLIP_PLANES];
};

layout(set = 0, binding = 0) uniform PerModel {
  mat4 vp_matrix;

  // Currently unused.
  vec2 frag_coord_to_uv_multiplier;
  float time;
  vec3 ambient_light_intensity;
  vec3 direct_light_intensity;
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

// Take the specified world-space vertex position and:
// - Transform it into screen-space homogeneous coordinates by multipying it by
//   the view-projection matrix.  Then write it into the special gl_Position
//   variable, which is used by the Vulkan rasterizer.
// - Clip the vertex in world space by taking the dot-product against each of
//   the specified clip-planes.  Then write each result into the special
//   gl_ClipDistance variable, which is used by the Vulkan rasterizer.
void ClipWorldSpaceAndOutputScreenSpaceCoords(vec4 world_pos) {
  gl_Position = vp_matrix * world_pos;

  for (int i = 0; i < NUM_CLIP_PLANES; ++i) {
    gl_ClipDistance[i] = dot(clip_planes[i], world_pos);
  }
}

#ifdef SHADOW_MAP_LIGHTING_PASS
// TODO(ES-109):
// |light_transform| is not currently defined.  There should be a light
// transform per light.  In the untested/unfinished code below, the transform is
// applied to the model-space vertex position; this requires the CPU to compute
// a separate matrix for each draw call.  It might be a better idea to specify
// the transform with respect to world space, since
// ClipWorldSpaceAndOutputScreenSpaceCoords() is already computing the
// world-space position in order to apply the clip-planes.
#error Not implemented.

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 shadowPos;
void main() {
  vec4 pos = ComputeVertexPosition();
  ClipWorldSpaceAndOutputScreenSpaceCoords(model_transform * pos);
  shadowPos = light_transform * pos;
  fragUV = inUV;
}
#endif

#ifdef NO_SHADOW_LIGHTING_PASS
layout(location = 0) out vec2 fragUV;
void main() {
  vec4 pos = ComputeVertexPosition();
  ClipWorldSpaceAndOutputScreenSpaceCoords(model_transform * pos);
  fragUV = inUV;
}
#endif

#if defined(DEPTH_PASS) || defined(SHADOW_MAP_GENERATION_PASS)
#error Not implemented.
void main() {
  ClipWorldSpaceAndOutputScreenSpaceCoords(
    model_transform * ComputeVertexPosition());
}
#endif
