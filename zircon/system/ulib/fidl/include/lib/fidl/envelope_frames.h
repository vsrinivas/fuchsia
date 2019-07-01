// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ENVELOPE_FRAMES_H_
#define LIB_FIDL_ENVELOPE_FRAMES_H_

#include <lib/fidl/coding.h>
#include <stdalign.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>
#include <cstdlib>

namespace fidl {

class EnvelopeFrames {
 public:
  struct EnvelopeState {
    uint32_t bytes_so_far;
    uint32_t handles_so_far;

    EnvelopeState(uint32_t bytes_so_far, uint32_t handles_so_far)
        : bytes_so_far(bytes_so_far), handles_so_far(handles_so_far) {}

   private:
    // Default constructor used by |EnvelopeFrames| to avoid unnecessarily zeroing
    // the |envelope_states_| array.
    EnvelopeState() = default;
    friend class EnvelopeFrames;
  };

  const EnvelopeState& Pop() {
    ZX_ASSERT(envelope_depth_ != 0);
    envelope_depth_--;
    return envelope_states_[envelope_depth_];
  }

  bool Push(const EnvelopeState& state) {
    if (envelope_depth_ == FIDL_RECURSION_DEPTH) {
      return false;
    }
    envelope_states_[envelope_depth_] = state;
    envelope_depth_++;
    return true;
  }

 private:
  uint32_t envelope_depth_ = 0;
  EnvelopeState envelope_states_[FIDL_RECURSION_DEPTH];
};

}  // namespace fidl

#endif  // LIB_FIDL_ENVELOPE_FRAMES_H_
