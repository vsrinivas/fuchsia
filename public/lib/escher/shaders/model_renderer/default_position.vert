// This is a fragment that is conditionally included in main.vert, when
// no vertex position modifiers (e.g. WOBBLE_VERTEX_POSITION) are enabled.

layout(set = 1, binding = 0) uniform PerObject {
  mat4 model_transform;
  mat4 light_transform;
  vec4 color;
};

vec4 ComputeVertexPosition() {
  return vec4(inPosition, 1);
}
