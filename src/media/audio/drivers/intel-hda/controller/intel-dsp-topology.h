// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_TOPOLOGY_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_TOPOLOGY_H_

#include <cstdint>

#include "intel-dsp-modules.h"

namespace audio {
namespace intel_hda {

// Represents a pipeline backing an audio stream.
struct DspPipeline {
  DspPipelineId id;
};

}  // namespace intel_hda
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CONTROLLER_INTEL_DSP_TOPOLOGY_H_
