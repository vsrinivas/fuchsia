// #included by main.frag

// TODO(ES-109): this code was extracted from the old ad-hoc shader generation
// classes so that whoever implements this doesn't have to work though that
// twisted logic.  But it hasn't ever been tested or run.
#error Not implemented.

// Common to all shadow map techniques.
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 shadowPos;

layout(set = 0, binding = 0) uniform PerModel {
  vec2 frag_coord_to_uv_multiplier;
  float time;
  vec3 ambient_light_intensity;
  vec3 direct_light_intensity;
  vec2 shadow_map_uv_multiplier;
};

layout(set = 0, binding = 1) uniform sampler2D shadow_map_tex;

layout(set = 1, binding = 0) uniform PerObject {
  mat4 camera_transform;
  // TODO(ES-109): See comment in main.vert: perhaps this shouldn't be per-obj.
  // Anyway, it's not defined in the vertex shaders, so it needs to go somewhere
  // else.
  // mat4 light_transform;
  vec4 color;
  vec4 clip_planes[NUM_CLIP_PLANES];
};

layout(set = 1, binding = 1) uniform sampler2D material_tex;


#ifndef MOMENT_SHADOW_MAP

// TODO: better fudge factor
const float kFudgeFactor = 1e-3;

float weight(float x, float y) {
  return abs(x) < 1. && abs(y) < 1. ?
      (.6 / 4.) : (.4 / 12.);
}

void main() {
  vec3 light = ambient_light_intensity;
  vec4 shadowUV = (shadowPos / shadowPos.w);
  float fragLightDist = shadowUV.z;

  float x, y;
  for (y = -1.5; y <= 1.5; y += 1.) {
    for (x = -1.5; x <= 1.5; x += 1.) {
      vec2 shadowCoord = shadowUV.xy + vec2(x, y) * shadow_map_uv_multiplier;
      float occluderLightDist = texture(shadow_map_tex, shadowCoord).r;
      if (occluderLightDist + kFudgeFactor >= fragLightDist) {
        light += weight(x, y) * direct_light_intensity;
      }
    }
  }
  outColor = vec4(light, 1.f) * color * texture(material_tex, inUV);
}

#else

// Solve x for Bx = z, where B is symmetric and positive semi-definite.
// https://en.wikipedia.org/wiki/Cholesky_decomposition
// https://en.wikipedia.org/wiki/Triangular_matrix
vec3 solveLinear(mat3 B, vec3 z) {
  // Compute the lower triangular matrix L.
  float D1 = B[0][0];
  float invD1 = 1. / B[0][0];
  float L21 = B[1][0] * invD1;
  float L31 = B[2][0] * invD1;
  float D2 = B[1][1] - L21 * L21 * D1;
  float invD2 = 1. / D2;
  float L32 = (B[2][1] - L31 * L21 * D1) * invD2;
  float D3 = B[2][2] - L31 * L31 * D1 - L32 * L32 * D2;
  float invD3 = 1. / D3;
  // Solve (D * L.T * x) with forward substitution.
  vec3 y;
  y[0] = z[0];
  y[1] = z[1] - L21 * y[0];
  y[2] = z[2] - L31 * y[0] - L32 * y[1];
  // Scale y to get (L.T * x).
  y *= vec3(invD1, invD2, invD3);
  // Solve x with backward substitution.
  vec3 x;
  x[2] = y[2];
  x[1] = y[1] - L32 * x[2];
  x[0] = y[0] - L21 * x[1] - L31 * x[2];
  return x;
}

float computeVisibility(vec4 moments, float fragLightDist) {
  // The magic numbers come from paper
  // http://cg.cs.uni-bonn.de/aigaion2root/attachments/MomentShadowMapping.pdf
  // The alpha (kMomentBias here) from paper is 3e-5, but that will cause a lot
  // shadow acne for us.
  const float kMomentBias = 4e-4;
  const float kDepthBias = 1e-6;
  vec4 b = mix(moments, vec4(.5, .5, .5, .5), kMomentBias);
  mat3 B = mat3(
      1.0, b.x, b.y,
      b.x, b.y, b.z,
      b.y, b.z, b.w);
  float zf = fragLightDist - kDepthBias;
  vec3 z = vec3(1., zf, zf * zf);
  vec3 c = solveLinear(B, z);
  float sqrtDelta = sqrt(max(0., c.y * c.y - 4. * c.z * c.x));
  float d1 = (-c.y - sqrtDelta) / (2. * c.z);
  float d2 = (-c.y + sqrtDelta) / (2. * c.z);
  if (d2 < d1) {
    float tmp = d1;
    d1 = d2;
    d2 = tmp;
  }
  if (zf <= d1) {
    return 1.;
  } else if (zf <= d2) {
    return 1. - (zf * d2 - b.x * (zf + d2) + b.y) / ((d2 - d1) * (zf - d1));
  } else {
    return (d1 * d2 - b.x * (d1 + d2) + b.y) / ((zf - d1) * (zf - d2));
  }
}

// The magic weights come from paper
// http://cg.cs.uni-bonn.de/aigaion2root/attachments/MomentShadowMapping.pdf
vec4 decode(vec4 y) {
  const mat4 kWeight = mat4(
    0.2227744146, 0.1549679261, 0.1451988946, 0.163127443,
    0.0771972861, 0.1394629426, 0.2120202157, 0.2591432266,
    0.7926986636, 0.7963415838, 0.7258694464, 0.6539092497,
    0.0319417555, -0.1722823173, -0.2758014811, -0.3376131734);
  const vec4 kBias = vec4(0.035955884801, 0., 0., 0.);
  return kWeight * (y - kBias);
}

void main() {
  vec4 shadowUV = shadowPos / shadowPos.w;
  float fragLightDist = shadowUV.z;
  vec4 shadowMapSample = texture(shadow_map_tex, shadowUV.xy);
  vec4 moments = decode(shadowMapSample);
  float visibility = computeVisibility(moments, fragLightDist);
  visibility = clamp(visibility, 0., 1.);
  vec3 light = ambient_light_intensity + visibility * direct_light_intensity;
  outColor = vec4(light, 1.) * color * texture(material_tex, inUV);
}

#endif
