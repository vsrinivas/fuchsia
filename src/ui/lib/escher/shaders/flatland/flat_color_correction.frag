#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_control_flow_attributes : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput input_color;

// The color correction data is passed along to the shader
// as push constants.
layout(std140, push_constant) uniform PushConstant {
  layout(offset=0) mat4 matrix;
  layout(offset=64) vec4 preoffset;
  layout(offset=80) vec4 postoffset;
};

layout(location = 0) out vec4 outColor;

void main() {
  // Simply apply the conversion formula to the input color.
  vec4 color = subpassLoad(input_color);
  outColor = matrix * (color + preoffset) + postoffset;
}
