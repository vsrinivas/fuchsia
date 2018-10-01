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
#define USE_PAPER_SHADER_CAMERA_AMBIENT 1
#define USE_PAPER_SHADER_MESH_INSTANCE 1
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
  gl_Position = vp_matrix * world_pos;

  #ifdef NUM_CLIP_PLANES
  for (int i = 0; i < NUM_CLIP_PLANES; ++i) {
    gl_ClipDistance[i] = dot(clip_planes[i], world_pos);
  }
  #endif
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

#ifdef EXTRUDE_SHADOW_VOLUME
void main() {
  #ifdef NUM_CLIP_PLANES
  #error Vertex clip planes are incompatible with shadow-volume extrusion.
  #endif

  vec4 world_pos = model_transform * ComputeVertexPosition();
  vec4 light_position =
      point_lights[PaperShaderPushConstants.light_index].position;
  // TODO(ES-160): optimize length of extrusion vec so that it doesn't
  // extend far below the floor of the stage.  This can improve performance
  // by reducing the number of stencil-buffer pixels that are touched.
  vec4 extrusion_vec =
      500.f * normalize(world_pos - light_position);
  gl_Position = vp_matrix * (world_pos + inBlendWeight.x * extrusion_vec);
}
#endif
