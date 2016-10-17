// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <queue>

#include "apps/media/src/audio/mixer_input.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

// Mixes any number of inputs to output buffers.
template <typename TOutSample>
class Mixer {
 public:
  Mixer(uint32_t out_channel_count) : out_channel_count_(out_channel_count) {
    FTL_DCHECK(out_channel_count > 0);
  }

  // Add an input to the mixer. The input and the mixer must agree on the
  // number of output channels.
  //
  // Inputs aren't asked to mix unless the last sample in a Mix call has a PTS
  // of input->first_pts() or less. Inputs are removed when their Mix method
  // returns false.
  void AddInput(std::shared_ptr<MixerInput<TOutSample>> input) {
    FTL_DCHECK(input);
    FTL_DCHECK(input->out_channel_count() == out_channel_count_);
    pending_inputs_.push(input);
  }

  // Mixes all inputs into the output buffer. The output buffer isn't
  // silenced initially, so the caller has to do it, as appropriate.
  // TODO(dalesat): Change semantics for first pass optimization.
  //
  // The intention is that the pts value for a given Mix call should be equal
  // to pts + out_frame_count from the previous Mix call, but any pts values
  // are tolerated. pts values are in frame units.
  void Mix(TOutSample* out_buffer, uint32_t out_frame_count, int64_t pts) {
    FTL_DCHECK(out_buffer);
    FTL_DCHECK(out_frame_count > 0);

    // Move ready inputs from the pending queue to the active list.
    while (!pending_inputs_.empty() &&
           pending_inputs_.top()->first_pts() < pts + out_frame_count) {
      active_inputs_.push_back(pending_inputs_.top());
      pending_inputs_.pop();
    }

    // Let all the active inputs mix to the buffer.
    auto iter = active_inputs_.begin();
    while (iter != active_inputs_.end()) {
      if ((*iter)->first_pts() >= pts + out_frame_count) {
        // We've backtracked, and this input isn't relevant. Put it back on the
        // pending queue.
        pending_inputs_.push(*iter);
        iter = active_inputs_.erase(iter);
      } else if ((*iter)->Mix(out_buffer, out_frame_count, pts)) {
        // Input is still relevant. Leave it in the list.
        ++iter;
      } else {
        // Done with this input. Remove it.
        iter = active_inputs_.erase(iter);
      }
    }
  }

 private:
  uint32_t out_channel_count_;
  std::priority_queue<std::shared_ptr<MixerInput<TOutSample>>> pending_inputs_;
  std::list<std::shared_ptr<MixerInput<TOutSample>>> active_inputs_;
};

}  // namespace media
}  // namespace mojo
