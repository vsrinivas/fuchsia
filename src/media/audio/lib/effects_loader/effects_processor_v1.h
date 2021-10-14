// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_V1_H_
#define SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_V1_H_

#include <zircon/types.h>

#include <vector>

#include "src/media/audio/lib/effects_loader/effect_v1.h"

namespace media::audio {

// EffectsProcessorV1 represents a queue of active effect instances and manages chaining calls of
// Process/ProcessInPlace through a chain of effects.
//
// This class is designed to be used synchronously and is not explicitly multi-thread-safe.
class EffectsProcessorV1 {
 public:
  // Creates a new, empty effects processor.
  EffectsProcessorV1() = default;

  // Disallow copy/move.
  EffectsProcessorV1(const EffectsProcessorV1&) = delete;
  EffectsProcessorV1& operator=(const EffectsProcessorV1&) = delete;
  EffectsProcessorV1(EffectsProcessorV1&& o) = delete;
  EffectsProcessorV1& operator=(EffectsProcessorV1&& o) = delete;

  // Adds an `EffectV1` to the end of the queue of effects included in this processor.
  //
  // When the first `EffectV1` is added, that effects input channels becomes the input to the entire
  // processor. Likewise that effects output channels becomes the processors output channels.
  //
  // When subsequent effects are added, the new effects input channels must match exactly the out-
  // put channels of last added effect. The output channels for the processor will be updated to
  // match the output channels of the newly added effect.
  //
  // Aborts if `e` is an invalid `EffectV1`.
  zx_status_t AddEffect(EffectV1 e);

  // Returns the number of active instances in the enclosed effect chain.
  [[nodiscard]] uint16_t size() const { return static_cast<uint16_t>(effects_chain_.size()); }

  // Returns the number of input channels for this effect. This will be the number of channels
  // expected for input frames to `Process` or `ProcessInPlace`.
  //
  // Returns 0 if this processor has no effects.
  [[nodiscard]] int32_t channels_in() const { return channels_in_; }

  // Returns the number of output channels for this effect.
  //
  // Returns 0 if this processor has no effects.
  [[nodiscard]] int32_t channels_out() const { return channels_out_; }

  // Returns the required block size (in frames) for this processor. Calls to |ProcessInPlace| must
  // provide frames in multiples of |block_size()|.
  [[nodiscard]] int64_t block_size() const { return block_size_; }

  // Returns the maximum buffer size (in frames) the processor is prepared to handle with a single
  // call to |ProcessInPlace| or |Process|.
  //
  // Returns 0 if the plugin can handle arbitrary buffer sizes.
  [[nodiscard]] int64_t max_batch_size() const { return max_batch_size_; }

  // Returns the number of frames the input signal will be delayed after being run through this
  // |EffectsProcessorV1|.
  [[nodiscard]] int64_t delay_frames() const { return delay_frames_; }

  // Returns the number of frames of silence that this processor requires to idle.
  [[nodiscard]] int64_t ring_out_frames() const { return ring_out_frames_; }

  [[nodiscard]] auto begin() { return effects_chain_.begin(); }
  [[nodiscard]] auto end() { return effects_chain_.end(); }
  [[nodiscard]] auto cbegin() const { return effects_chain_.cbegin(); }
  [[nodiscard]] auto cend() const { return effects_chain_.cend(); }

  // Returns the instance at the specified (zero-based) position in the chain.
  [[nodiscard]] const EffectV1& GetEffectAt(int64_t position) const;

  // These maps directly to the corresponding ABI call, for each instance.
  zx_status_t ProcessInPlace(int64_t num_frames, float* audio_buff_in_out) const;
  zx_status_t Process(int64_t num_frames, float* audio_buff_in, float** audio_buff_out) const;
  zx_status_t Flush() const;
  void SetStreamInfo(const fuchsia_audio_effects_stream_info& stream_info) const;

 private:
  std::vector<EffectV1> effects_chain_;
  std::vector<fuchsia_audio_effects_parameters> effects_parameters_;

  int32_t channels_in_ = 0;
  int32_t channels_out_ = 0;
  int64_t block_size_ = 1;
  int64_t max_batch_size_ = 0;
  int64_t delay_frames_ = 0;
  int64_t ring_out_frames_ = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_EFFECTS_LOADER_EFFECTS_PROCESSOR_V1_H_
