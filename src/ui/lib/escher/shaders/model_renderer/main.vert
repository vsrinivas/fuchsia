#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
  vec4 gl_Position;

  #ifdef NUM_CLIP_PLANES
  float gl_ClipDistance[NUM_CLIP_PLANES];
  #endif
};

// Definitions here are used by all programs.  The C++ code which creates the
// ShaderProgram will add other USE_* definitions that are appropriate for that
// shader variant.
#define IS_VERTEX_SHADER 1
#define USE_PAPER_SHADER_MESH_INSTANCE 1
#define USE_PAPER_SHADER_LATCHED_POSEBUFFER 1
#define USE_ATTRIBUTE_POSITION 1
#include "shaders/paper/common/use.glsl"

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
  gl_Position = vp_matrix[PaperShaderPushConstants.eye_index] * world_pos;

  #ifdef NUM_CLIP_PLANES
  for (int i = 0; i < NUM_CLIP_PLANES; ++i) {
    gl_ClipDistance[i] = dot(clip_planes[i], world_pos);
  }
  #endif
}

#ifdef SHADOW_MAP_LIGHTING_PASS
// TODO(fxbug.dev/7200):
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

#ifdef SHADOW_VOLUME_POINT_LIGHTING
layout(location = 0) out vec2 fragUV;

#ifdef USE_PAPER_SHADER_POINT_LIGHT_FALLOFF
layout(location = 1) out vec4 irradiance;
#endif // USE_PAPER_SHADER_POINT_LIGHT_FALLOFF

void main() {
  vec4 world_pos = model_transform * ComputeVertexPosition();
  gl_Position = vp_matrix[PaperShaderPushConstants.eye_index] * world_pos;
  fragUV = inUV;

#ifdef USE_PAPER_SHADER_POINT_LIGHT_FALLOFF
  // Irradiance.  Compute an attenuation factor based on the distance to the
  // light, and apply it to the incoming light color/intensity.  The attenuation
  // is based on the inverse square law, with a falloff adjustment to prevent
  // it from dropping off too rapidly.
  vec3 light_position =
      point_lights[PaperShaderPushConstants.light_index].position.xyz;
  float falloff = point_lights[PaperShaderPushConstants.light_index].falloff.x;
  vec3 adjusted_light = falloff * (light_position - world_pos.xyz);
  float attenuation = 1.f / (1.f + dot(adjusted_light, adjusted_light));
  irradiance = attenuation *
      point_lights[PaperShaderPushConstants.light_index].color;
#endif  // USE_PAPER_SHADER_POINT_LIGHT_FALLOFF
}
#endif  // SHADOW_VOLUME_POINT_LIGHTING

#ifdef SHADOW_VOLUME_EXTRUSION
void main() {
  #ifdef NUM_CLIP_PLANES
  #error Vertex clip planes are incompatible with shadow-volume extrusion.
  #endif

  vec4 world_pos = model_transform * ComputeVertexPosition();
  vec4 light_position =
      point_lights[PaperShaderPushConstants.light_index].position;
  // TODO(fxbug.dev/7251): optimize length of extrusion vec so that it doesn't
  // extend far below the floor of the stage.  This can improve performance
  // by reducing the number of stencil-buffer pixels that are touched.  On the
  // other hand, ensure that it extends far enough: there will be artifacts when
  // the shadow volume does not extend far enough (especially likely at glancing
  // shadow angles).
  const float kShadowVolumeExtrusionLength = 500.f;
  vec4 extrusion_vec =
      kShadowVolumeExtrusionLength * normalize(world_pos - light_position);
  gl_Position = vp_matrix[PaperShaderPushConstants.eye_index] *
      (world_pos + inBlendWeight.x * extrusion_vec);
}
#endif
