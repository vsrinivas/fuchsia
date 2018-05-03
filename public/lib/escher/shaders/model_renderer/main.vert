#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  gl_Position = vec4(1, 0, 0, 0);
}
