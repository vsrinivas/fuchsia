// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <unistd.h>
#include <zircon/assert.h>

#include <limits>

#include <soc/as370/audio-dsp.h>
// Cascaded integrator–comb filter.
// TODO(andresoportus) generalize and place in signal processing library.
#if defined(__clang__)
// Integrator and differentiator states are allowed to overflow and wrap,
// differentiator undoes intergrator's overflow and wrapping.
[[clang::no_sanitize("undefined")]]
#endif
uint32_t
CicFilter::Filter(uint32_t index,  // e.g. 0.
                  void* input,
                  uint32_t input_size,  // e.g. 16K.
                  void* output,
                  uint32_t input_total_channels,   // e.g. 2.
                  uint32_t input_channel,          // e.g. 0 or 1.
                  uint32_t output_total_channels,  // e.f. 2.
                  uint32_t output_channel,         // e.g. 0  or 1.
                  uint32_t multiplier_shift) {
#ifdef TESTING_CAPTURE_PDM
  static_cast<void>(integrator_state);
  static_cast<void>(differentiator_state);

  // Since input bits per channel is 32, we use 32 bits pointers.
  uint32_t* in = static_cast<uint32_t*>(input);
  uint32_t* in_end = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(input) + input_size);

  // Since output bits per channel is 32, we use 32 bits pointers.
  uint32_t* out = static_cast<uint32_t*>(output);

  if (index > kMaxIndex) {
    return 0;
  }

  uint32_t amount_pcm = 0;
  while (in < in_end) {
    uint32_t bits = in[input_channel];
    in += input_total_channels;
    out[output_channel] = bits;
    out += output_total_channels;
    amount_pcm += static_cast<uint32_t>(output_total_channels * sizeof(int32_t));
  }
#else

  // Since input bits per channel is 32, we use 32 bits pointers.
  uint32_t* in = static_cast<uint32_t*>(input);
  uint32_t* in_end = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(input) + input_size);

  // 16 output bits per channel.
  ZX_ASSERT(kOutputBitsPerSample == 16);
  uint16_t* out = static_cast<uint16_t*>(output);

  if (index > kMaxIndex) {
    return 0;
  }

  uint32_t amount_pcm = 0;
  while (in < in_end) {
    // Integrate.
    for (size_t word = 0; word < kInputBitsPerSample / (sizeof(uint32_t) * 8); ++word) {
      uint32_t bits = in[input_channel];
      in += input_total_channels;
      for (uint32_t i = 0; i < sizeof(uint32_t) * 8; ++i, bits >>= 1) {
        // Integrator state is allowed to overflow and wrap, this is ok becuase of modulo
        // arithmetic, the differentiation will undo the wrapping.
        auto plus_or_minus = (static_cast<int32_t>(bits & 1) << 1) - 1;  // +1/-1 from 1/0;
        integrator_state[index][0] += plus_or_minus;
        for (uint32_t stage = 1; stage < kOrder; ++stage) {
          integrator_state[index][stage] += integrator_state[index][stage - 1];
        }
      }
    }

    // COMB (differentiator).
    int32_t acc = integrator_state[index][kOrder - 1];
    int32_t old_acc = 0;
    for (uint32_t stage = 0; stage < kOrder; ++stage) {
      old_acc = acc;
      acc -= differentiator_state[index][stage];
      differentiator_state[index][stage] = old_acc;
    }

    // Output pre-amplified by multiplier_shift.
    int32_t result = acc;
    if (result >= ((1 << (31 - multiplier_shift)) - 1)) {
      result = std::numeric_limits<int32_t>::max();
    } else {
      result <<= multiplier_shift;
    }

    // 16 output bits per channel.
    out[output_channel] = static_cast<int16_t>((result - dc[index]) >> 16);
    out += output_total_channels;
    amount_pcm += static_cast<uint32_t>(output_total_channels * sizeof(int16_t));

    // DC is calculated via a low pass filter as an exponentially weighted moving average
    // using a constant k = 1 / 4096 that makes the calculation fast and has a frequency corner
    // fc = k / ((1 - k) x 2 x pi x dT) = 1.87Hz for a 48kHz rate input (dT = 1 / 48K = 20.83usecs).
    // TODO(andresoportus): Potential improvements include a more sophisticated CIC filter,
    // configuration via metadata of the parameters used (here shift_dc_filter), and paralelization
    // of the filtering process.
    constexpr uint32_t shift_dc_filter = 12;
    dc[index] += (result - dc[index]) >> shift_dc_filter;
  }
#endif
  return amount_pcm;
}
