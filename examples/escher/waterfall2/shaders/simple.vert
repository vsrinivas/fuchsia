#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
  vec4 gl_Position;
};

layout(set = 0, binding = 0) uniform ViewProjection {
  mat4 vp_matrix;
};

layout(set = 1, binding = 0) uniform ObjectProperties {
  mat4 model_transform;
  vec4 color;
};

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 colorOut;

void main() {
  gl_Position = vp_matrix * model_transform * vec4(inPosition, 1);
  fragUV = inUV;
  colorOut = color;
}
