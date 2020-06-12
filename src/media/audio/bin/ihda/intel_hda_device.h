// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_BIN_IHDA_INTEL_HDA_DEVICE_H_
#define SRC_MEDIA_AUDIO_BIN_IHDA_INTEL_HDA_DEVICE_H_

#include <stdint.h>

#include "zircon_device.h"

namespace audio {
namespace intel_hda {

struct IntelHDADevice {
  uint16_t vid;
  uint16_t did;
  uint8_t ihda_vmaj;
  uint8_t ihda_vmin;
  uint8_t rev_id;
  uint8_t step_id;
};

zx_status_t ProbeIntelHdaDevice(ZirconDevice* device, IntelHDADevice* result);

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_BIN_IHDA_INTEL_HDA_DEVICE_H_
