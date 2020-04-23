// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AUDIO_DSP_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AUDIO_DSP_H_

#include <assert.h>

#include <memory>
#include <utility>

//#define TESTING_CAPTURE_PDM

// TODO(andresoportus) generalize and place in signal processing library.
class CicFilter {
 public:
  explicit CicFilter() = default;
  virtual ~CicFilter() = default;
  virtual uint32_t Filter(uint32_t index, void* input, uint32_t input_size, void* output,
                          uint32_t input_total_channels, uint32_t input_channel,
                          uint32_t output_total_channels, uint32_t output_channel,
                          uint32_t multiplier_shift);  // virtual for unit testing.
  uint32_t GetInputToOutputRatio() { return kInputBitsPerSample / kOutputBitsPerSample; }

 private:
  static constexpr uint32_t kMaxIndex = 4;
  static constexpr uint32_t kOrder = 5;
  static constexpr uint32_t kInputBitsPerSample = 64;
  static constexpr uint32_t kOutputBitsPerSample = 16;

  int32_t integrator_state[kMaxIndex][kOrder] = {};
  int32_t differentiator_state[kMaxIndex][kOrder] = {};
  int32_t dc[kMaxIndex] = {};
};

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AUDIO_DSP_H_
