// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_AUDIO_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_AUDIO_H_

#include <ddktl/metadata/audio.h>

namespace metadata {

enum class AmlVersion : uint32_t {
  kS905D2G = 1,  // Also works with T931G.
  kS905D3G = 2,
};

enum class AmlBus : uint32_t {
  TDM_A = 1,
  TDM_B = 2,
  TDM_C = 3,
};

struct AmlConfig {
  bool is_input;
  uint8_t number_of_channels;
  AmlBus bus;
  AmlVersion version;
  Tdm tdm;
};

}  // namespace metadata

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_AUDIO_H_
