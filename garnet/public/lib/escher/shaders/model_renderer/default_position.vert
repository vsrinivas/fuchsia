// This is a fragment that is conditionally included in main.vert, when
// no vertex position modifiers (e.g. WOBBLE_VERTEX_POSITION) are enabled.

vec4 ComputeVertexPosition() {
  return vec4(inPosition, 1);
}
