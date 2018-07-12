#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 outColor;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(set = 1, binding = 1) uniform sampler2D material_tex;

void main() {
  outColor = texture(material_tex, inUV) * inColor;
}
