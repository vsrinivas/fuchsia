// #included by main.frag

// TODO(ES-109): this code was extracted from the old ad-hoc shader generation
// classes so that whoever implements this doesn't have to work though that
// twisted logic.  But it hasn't ever been tested or run.
#error Not implemented.

#ifndef MOMENT_SHADOW_MAP
// Regular shadow map simply returns the depth.
vec4 EncodeDepth(float depth) {
  return vec4(depth);
}
#else
// Moment shadow map first computes the 1st-4th moments of the depth, and
// encodes them to make optimal use of available precision.
vec4 EncodeDepth(float z) {
  float z2 = z * z;
  float z3 = z * z2;
  float z4 = z * z3;
  vec4 moments(z, z2, z3, z4);

  // The magic weights come from paper
  // http://cg.cs.uni-bonn.de/aigaion2root/attachments/MomentShadowMapping.pdf
  const mat4 kWeight = mat4(
    -2.07224649, 13.7948857237, 0.105877704, 9.7924062118,
    32.23703778, -59.4683975703, -1.9077466311, -33.7652110555,
    -68.571074599, 82.0359750338, 9.3496555107, 47.9456096605,
    39.3703274134, -35.364903257, -6.6543490743, -23.9728048165);
  const vec4 kBias = vec4(0.035955884801, 0., 0., 0.);

  return kWeight * moments + kBias;
}

#endif

void main() {
  outColor = EncodeDepth(gl_FragCoord.z);
}
