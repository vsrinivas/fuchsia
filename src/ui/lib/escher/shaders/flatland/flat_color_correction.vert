#version 450
#extension GL_ARB_separate_shader_objects : enable

// Untransformed vertex positions in clockwise order, starting
// at the top left corner. These form a triangle that fully
// covers the screen in clip space.
vec2 positions[3] = vec2[](
  vec2(-1,-1),
  vec2(3,-1),
  vec2(-1,3)
);

// Use gl_VertexIndex to index into the hardcoded rectangle
// positions array.
void main() {
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0, 1.0);
}

