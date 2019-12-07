#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUVCoords;


// Inverse 3D extent which contains the maximum extent
// in XYZ space that rectangles exist in. This value
// is used to normalize the inPosition of each vertex
// to be in the range [0,1].
layout(push_constant) uniform PushConstant {
  layout(offset=0) vec3 inverse_extent;
};

layout(location = 0) out vec2 outUVCoords;

// Expect vertices to be passed into the vertex shader already
// using NDC so no extra matrix multiplication or processing
// is needed.
void main() {
    gl_Position = vec4(inPosition * inverse_extent, 1.0);
    outUVCoords = inUVCoords;
}