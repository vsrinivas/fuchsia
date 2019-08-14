// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_H_

#include <zircon/types.h>

#include <vector>

#include "src/media/audio/lib/effects_loader/effect.h"

namespace media::audio {

// EffectsProcessor represents a chain of active effect instances and manages chaining calls of
// Process/ProcessInPlace through a chain of effects.
//
// This class is designed to be used synchronously and is not explicitly multi-thread-safe.
class EffectsProcessor {
 public:
  void AddEffect(Effect e);

  // Returns the number of active instances in the enclosed effect chain.
  [[nodiscard]] uint16_t size() const { return effects_chain_.size(); }

  [[nodiscard]] auto begin() { return effects_chain_.begin(); }
  [[nodiscard]] auto end() { return effects_chain_.end(); }
  [[nodiscard]] auto cbegin() const { return effects_chain_.cbegin(); }
  [[nodiscard]] auto cend() const { return effects_chain_.cend(); }

  // Returns the instance at the specified (zero-based) position in the chain.
  [[nodiscard]] const Effect& GetEffectAt(size_t position) const;

  // These maps directly to the corresponding ABI call, for each instance.
  zx_status_t ProcessInPlace(uint32_t num_frames, float* audio_buff_in_out) const;
  zx_status_t Flush() const;

 private:
  std::vector<Effect> effects_chain_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_H_
