#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "shaders/paper/vertex_attributes.vert"

#define SHADOW_VOLUME_EXTRUDE 1

#include "shaders/paper/compute_model_space_position.vert"
#include "shaders/paper/compute_world_space_position.vert"

#if NUM_CLIP_PLANES > 0
#error GPU clip planes are incompatible with shadow volume extrusion.
#endif

void main() {
  vec4 model_pos = ComputeModelSpacePosition();
  vec4 world_pos = ComputeWorldSpacePosition(model_pos);
  gl_Position = world_pos;
}
