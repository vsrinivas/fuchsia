#version 450
#extension GL_ARB_separate_shader_objects : enable

#ifndef SHADOW_VOLUME_EXTRUSION
#error SHADOW_VOLUME_EXTRUSION must be defined.
#endif

#ifdef NUM_CLIP_PLANES
#error GPU clip planes are incompatible with shadow volume extrusion.
#endif

#ifdef USE_ATTRIBUTE_POSITION
#error UV coords are not passed on to fragment shader.
#endif

// Define macros before importing 'use.glsl', to enable various required features.
#define IS_VERTEX_SHADER 1
#define USE_PAPER_SHADER_MESH_INSTANCE 1
#define USE_PAPER_SHADER_LATCHED_POSEBUFFER 1
#define USE_ATTRIBUTE_POSITION 1
#include "shaders/paper/common/use.glsl"

void main() {
  vec4 world_pos = model_transform * vec4(inPosition, 1);
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
