// If USE_FOO is defined, then use the corresponding descriptor set binding.

// ***** VERTEX ATTRIBUTES **********************************

#ifdef IS_VERTEX_SHADER

// 'location' corresponds to PaperRenderFuncs::kMeshAttributeBindingLocations::position_2d/3d.
#ifdef USE_ATTRIBUTE_POSITION
layout(location = 0) in vec3 inPosition;
#endif

// 'location' corresponds to PaperRenderFuncs::kMeshAttributeBindingLocations::position_offset.
#ifdef USE_ATTRIBUTE_POSITION_OFFSET
layout(location = 1) in vec3 inPositionOffset;
#endif

// 'location' corresponds to PaperRenderFuncs::kMeshAttributeBindingLocations::uv.
#ifdef USE_ATTRIBUTE_UV
layout(location = 2) in vec2 inUV;
#endif

// 'location' corresponds to PaperRenderFuncs::kMeshAttributeBindingLocations::perimeter_pos.
#ifdef USE_ATTRIBUTE_PERIMETER
layout(location = 3) in vec2 inPerimeter;
#endif

// 'location' corresponds to PaperRenderFuncs::kMeshAttributeBindingLocations::blend_weight1.
#ifdef USE_ATTRIBUTE_BLEND_WEIGHT_1
layout(location = 4) in vec2 inBlendWeight;
#endif

#endif  // IS_VERTEX_SHADER

// ***** DESCRIPTOR BINDINGS ********************************

#ifdef USE_PAPER_SHADER_SCENE_DATA
// Struct that defines a grepable common layout for C++ and GLSL code.
layout(set = 0, binding = 0) uniform PaperShaderSceneData {
  vec3 ambient_light_color;
};
#endif  // USE_PAPER_SHADER_SCENE_DATA

#ifdef USE_PAPER_SHADER_LATCHED_POSEBUFFER
layout(set = 0, binding = 1) uniform PaperShaderLatchedPoseBuffer {
  // Padding same size as escher::hmd::Pose.
  vec4 _padding_PaperShaderLatchedPoseBuffer[2];
  // Latched view-projection matrices for the two eyes.  The left eye is at
  // index 0 and the right at index 1.  See the |eye_index| field of
  // PaperShaderPushConstants.
  mat4 vp_matrix[2];
};
#endif  // USE_PAPER_SHADER_LATCHED_POSEBUFFER

#ifdef USE_PAPER_SHADER_POINT_LIGHT
// Struct that defines a grepable common layout for C++ and GLSL code.
struct PaperShaderPointLight {
  vec4 position;  // world-space
  vec4 color;     // RGB intensities
  // Only the x component is used, the rest are padding to handle GLSL packing
  // requirements.
  vec4 falloff;
};

layout(set = 0, binding = 2) uniform PointLightUniforms {
  // TODO(fxbug.dev/7244): don't clamp to 2.  Should provide the number of lights as
  // a pre-processor constant (this doesn't mean that we need to use them all).
  PaperShaderPointLight point_lights[2];
};
#endif  // USE_PAPER_SHADER_POINT_LIGHT

#ifdef USE_PAPER_SHADER_MESH_INSTANCE
layout(set = 1, binding = 0) uniform PaperShaderMeshInstance {
  mat4 model_transform;
  vec4 model_color;
};
#endif  // USE_PAPER_SHADER_MESH_INSTANCE

#ifdef USE_PAPER_SHADER_MESH_MATERIAL
// TODO(fxbug.dev/7250): paper_shader_structs.h gives us a decent framework for
// uniform bindings, but not for texture bindings.
layout(set = 1, binding = 1) uniform sampler2D material_tex;
#endif  // USE_PAPER_SHADER_MESH_MATERIAL

// ***** PUSH CONSTANTS ********************************

#ifdef USE_PAPER_SHADER_PUSH_CONSTANTS
layout(push_constant) uniform PushConstants {
  uint light_index;
  uint eye_index;
} PaperShaderPushConstants;
#endif  // USE_PAPER_SHADER_PUSH_CONSTANTS
