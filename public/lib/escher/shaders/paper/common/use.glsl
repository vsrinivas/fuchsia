// If USE_FOO is defined, then use the corresponding descriptor set binding.

// ***** VERTEX ATTRIBUTES **********************************

#ifdef IS_VERTEX_SHADER

#ifdef USE_ATTRIBUTE_POSITION
layout(location = 0) in vec3 inPosition;
#endif

#ifdef USE_ATTRIBUTE_POSITION_OFFSET
layout(location = 1) in vec3 inPositionOffset;
#endif

#ifdef USE_ATTRIBUTE_UV
layout(location = 2) in vec2 inUV;
#endif

#ifdef USE_ATTRIBUTE_PERIMETER
layout(location = 3) in vec2 inPerimeter;
#endif

#ifdef USE_ATTRIBUTE_BLEND_WEIGHT_1
layout(location = 4) in vec2 inBlendWeight;
#endif

#endif  // IS_VERTEX_SHADER

// ***** DESCRIPTOR BINDINGS ********************************

#ifdef USE_PAPER_SHADER_CAMERA_AMBIENT
layout(set = 0, binding = 0) uniform PaperShaderCameraAmbient {
  mat4 vp_matrix;
  vec3 ambient_light_color;
};
#endif  // USE_PAPER_SHADER_CAMERA_AMBIENT

#ifdef USE_PAPER_SHADER_POINT_LIGHT
struct PointLight {
  vec4 position;  // world-space
  vec4 color;     // RGB intensities
};

layout(set = 0, binding = 1) uniform PointShaderPointLight {
  // TODO(ES-153): don't clamp to 2.  Should provide the number of lights as
  // a pre-processor constant (this doesn't mean that we need to use them all).
  PointLight point_lights[2];
};
#endif  // USE_PAPER_SHADER_POINT_LIGHT

#ifdef USE_PAPER_SHADER_MESH_INSTANCE
layout(set = 1, binding = 0) uniform PaperShaderMeshInstance {
  mat4 model_transform;
  vec4 model_color;
};
#endif  // USE_PAPER_SHADER_MESH_INSTANCE

#ifdef USE_PAPER_SHADER_MESH_MATERIAL
// TODO(ES-159): paper_shader_structs.h gives us a decent framework for
// uniform bindings, but not for texture bindings.
layout(set = 1, binding = 1) uniform sampler2D material_tex;
#endif  // USE_PAPER_SHADER_MESH_MATERIAL

// ***** PUSH CONSTANTS ********************************

#ifdef USE_PAPER_SHADER_PUSH_CONSTANTS
layout(push_constant) uniform PushConstants {
  uint light_index;
} PaperShaderPushConstants;
#endif  // USE_PAPER_SHADER_PUSH_CONSTANTS
