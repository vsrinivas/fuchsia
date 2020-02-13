// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_TOPOLOGY_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_TOPOLOGY_H_

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

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_INTEL_DSP_TOPOLOGY_H_
