#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 uvCoords;

layout(set = 0, binding = 0) uniform sampler2D image;

layout(std140, push_constant) uniform PushConstant {
  layout(offset=16) vec4 color;
};

layout(location = 0) out vec4 outColor;


// Simply output the diffuse color with no other texturing or lighting.
void main() {
  outColor = color * texture(image, uvCoords);
}