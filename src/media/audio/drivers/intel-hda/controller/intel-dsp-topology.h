// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_TOPOLOGY_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_TOPOLOGY_H_

#include <cstdint>

#include "intel-dsp-modules.h"
#include "nhlt.h"

namespace audio {
namespace intel_hda {

// Represents a pipeline backing an audio stream.
struct DspPipeline {
  DspPipelineId id;
};

zx_status_t GetNhltBlob(const Nhlt& nhlt, uint8_t bus_id, uint8_t direction, uint8_t type,
                        const AudioDataFormat& format, const void** out_blob, size_t* out_size);
StatusOr<std::vector<uint8_t>> GetModuleConfig(const Nhlt& nhlt, uint8_t i2s_instance_id,
                                               uint8_t direction, uint8_t type,
                                               const CopierCfg& base_cfg);

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_TOPOLOGY_H_
