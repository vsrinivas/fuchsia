// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_PCI_REGS_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_PCI_REGS_H_

#include <cstdint>

namespace audio::intel_hda {

constexpr uint32_t kPciRegCgctl = 0x48;  // Clock Gating Control.

// Miscellaneous Backbone Dynamic Clock Gating Enable.
constexpr uint32_t kPciRegCgctlBitMaskMiscbdcge = (1 << 6);

}  // namespace audio::intel_hda

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_PCI_REGS_H_
