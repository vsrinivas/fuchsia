// This is a fragment that is included in main.vert when WOBBLE_VERTEX_POSITION
// is enabled.

// TODO: unused.  See discussion in PerObject struct, below.
struct SineParams {
  float speed;
  float amplitude;
  float frequency;
};
const int kNumSineParams = 3;
float EvalSineParams(SineParams params) {
  float arg = params.frequency * inPerimeter + params.speed * time;
  return params.amplitude * sin(arg);
}

layout(set = 1, binding = 0) uniform PerObject {
  mat4 model_transform;
  mat4 light_transform;
  vec4 color;
  // Corresponds to ModifierWobble::SineParams[0].
  float speed_0;
  float amplitude_0;
  float frequency_0;
  // Corresponds to ModifierWobble::SineParams[1].
  float speed_1;
  float amplitude_1;
  float frequency_1;
  // Corresponds to ModifierWobble::SineParams[2].
  float speed_2;
  float amplitude_2;
  float frequency_2;
  // TODO: for some reason, I can't say:
  //   SineParams sine_params[kNumSineParams];
  // nor:
  //   SineParams sine_params_0;
  //   SineParams sine_params_1;
  //   SineParams sine_params_2;
  // ... if I try, the GLSL compiler produces SPIR-V, but the "SC"
  // validation layer complains when trying to create a vk::ShaderModule
  // from that SPIR-V.  Note: if we ignore the warning and proceed, nothing
  // explodes.  Nevertheless, we'll leave it this way for now, to be safe.
};

// TODO: workaround.  See discussion in PerObject struct, above.
float EvalSineParams_0() {
  float arg = frequency_0 * inPerimeter + speed_0 * time;
  return amplitude_0 * sin(arg);
}
float EvalSineParams_1() {
  float arg = frequency_1 * inPerimeter + speed_1 * time;
  return amplitude_1 * sin(arg);
}
float EvalSineParams_2() {
  float arg = frequency_2 * inPerimeter + speed_2 * time;
  return amplitude_2 * sin(arg);
}

vec4 ComputeVertexPosition() {
  // TODO: workaround.  See discussion in PerObject struct, above.
  // float scale = EvalSineParams(sine_params_0) +
  //               EvalSineParams(sine_params_1) +
  //               EvalSineParams(sine_params_2);
  float offset_scale = EvalSineParams_0() + EvalSineParams_1() + EvalSineParams_2();
  return vec4(inPosition + offset_scale * inPositionOffset, 1);
}
