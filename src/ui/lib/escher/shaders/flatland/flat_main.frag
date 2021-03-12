#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_control_flow_attributes : enable

layout(location = 0) in vec2 uvCoords;

layout(set = 0, binding = 0) uniform sampler2D image;

layout(std140, push_constant) uniform PushConstant {
  layout(offset=80) vec4 color;
  layout(offset=96) float gamma_power;
};

layout(location = 0) out vec4 outColor;

void main() {
  outColor = color;
  vec4 material_color = texture(image, uvCoords);

  [[dont_flatten]]
  if (gamma_power != 1.0) {
    // Gamma correction is required.  Check whether we can avoid pow().
    [[dont_flatten]]
    if (gamma_power == 2.0) {
      // Squaring is cheaper than pow().
      material_color.rgb *= material_color.rgb;
    } else {
      // pow() is required in the general case.
      material_color.rgb = pow(material_color.rgb, vec3(gamma_power));
    }
  }
  outColor *= material_color;
}
