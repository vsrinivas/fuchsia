// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_H_

#include <zircon/types.h>

#include <vector>

#include "src/media/audio/lib/effects_loader/effect.h"

namespace media::audio {

// EffectsProcessor represents a queue of active effect instances and manages chaining calls of
// Process/ProcessInPlace through a chain of effects.
//
// This class is designed to be used synchronously and is not explicitly multi-thread-safe.
class EffectsProcessor {
 public:
  // Creates a new, empty effects processor.
  EffectsProcessor() = default;

  // Disallow copy/move.
  EffectsProcessor(const EffectsProcessor&) = delete;
  EffectsProcessor& operator=(const EffectsProcessor&) = delete;
  EffectsProcessor(EffectsProcessor&& o) = delete;
  EffectsProcessor& operator=(EffectsProcessor&& o) = delete;

  // Adds an `Effect` to the end of the queue of effects included in this processor.
  //
  // When the first `Effect` is added, that effects input channels becomes the input to the entire
  // processor. Likewise that effects output channels becomes the processors output channels.
  //
  // When subsequent effects are added, the new effects input channels must match exactly the out-
  // put channels of last added effect. The output channels for the processor will be updated to
  // match the output channels of the newly added effect.
  //
  // Currently AddEffect will fail if an Effect has channels_in != channels_out (ex: all effects
  // must do in-place processing). This is a short-term limitation that will be lifted in the
  // future.
  //
  // Aborts if `e` is an invalid `Effect`.
  zx_status_t AddEffect(Effect e);

  // Returns the number of active instances in the enclosed effect chain.
  [[nodiscard]] uint16_t size() const { return effects_chain_.size(); }

  // Returns the number of input channels for this effect. This will be the number of channels
  // expected for input frames to `ProcessInPlace`.
  //
  // Returns 0 if this processor has no effects.
  [[nodiscard]] uint32_t channels_in() const { return channels_in_; }

  // Returns the number of output channels for this effect. Currently this always equals
  // `channels_in()`.
  //
  // Returns 0 if this processor has no effects.
  [[nodiscard]] uint32_t channels_out() const { return channels_out_; }

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
  uint32_t channels_in_ = 0;
  uint32_t channels_out_ = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_H_
