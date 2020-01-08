#version 450
#extension GL_ARB_separate_shader_objects : enable

// Untransformed vertex positions in clockwise order, starting
// at the top left corner.
vec2 positions[4] = vec2[](
  vec2(0,0),
  vec2(1,0),
  vec2(1,1),
  vec2(0,1)
);

// This shader avoids the need for binding index and vertex buffers; this array
// plays a key role.
//
// Index buffers are avoided by using vkCmdDraw() instead of vkCmdDrawIndexed().
// This array is used to remap gl_VertexIndex into the range [0-3], because a
// rectangle has 4 vertices, not 6.
//
// Vertex buffers are avoided by using the remapped index to look up the
// position/UV coords corresponding to the current vertex. Position coords
// are hardcoded into the shader, and UV coords are provided as push constants.
uint remapped_indices[6] = uint[](
  0,1,2,0,2,3
);

// Push constants needed to render each RectangleRenderable.
layout(push_constant) uniform PushConstant {
  // Normalizing bounds for the renderable.
  // This is the only per-batch value; the others below
  // are updated per-renderable.
  layout(offset=0) vec3 bounds;

  // Renderable origin (top-left corner).
  layout(offset=16) vec3 origin;

  // Width and height of the renderable.
  layout(offset=32) vec2 extent;

  // UV coords in clockwise order, starting at top left.
  layout(offset=40) vec2 uvs[4];
};

// UV Coordinates used for the renderable texture in
// the fragment shader.
layout(location = 0) out vec2 outUVCoords;

// Use gl_VertexIndex to index into the hardcoded rectangle
// positions array. These positions are then transformed
// into clip-space based on the PushConstant data for a
// particular rectangle instance.
void main() {
    const uint index = remapped_indices[gl_VertexIndex];

    // Transform and normalize the position and output the
    // result to gl_Position.
    vec2 pos = positions[index] * extent + origin.xy;
    vec3 clip_pos = vec3(pos.xy - bounds.xy, origin.z) / bounds;
    gl_Position = vec4(clip_pos,  1.0);

    // We copy to a local array because gl_VertexIndex isn't
    // dynamically uniform, therefore neither is the remapped index,
    // so it can't be used as an index into the push constant array.
    vec2 local_uvs[4] = vec2[](uvs[0], uvs[1], uvs[2], uvs[3]);
    outUVCoords = local_uvs[index];
}

