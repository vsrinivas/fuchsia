// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_TI_INCLUDE_TI_TI_AUDIO_H_
#define SRC_DEVICES_LIB_TI_INCLUDE_TI_TI_AUDIO_H_

#include <stdint.h>

namespace metadata::ti {

static constexpr uint32_t kMaxNumberOfRegisterWrites = 256;

struct RegisterSetting {
  uint8_t address;
  uint8_t value;
};

struct TasConfig {
  bool bridged;
  uint8_t instance_count;
  uint8_t number_of_writes1;
  RegisterSetting init_sequence1[kMaxNumberOfRegisterWrites];
  uint8_t number_of_writes2;
  RegisterSetting init_sequence2[kMaxNumberOfRegisterWrites];
};

}  // namespace metadata::ti

#endif  // SRC_DEVICES_LIB_TI_INCLUDE_TI_TI_AUDIO_H_
