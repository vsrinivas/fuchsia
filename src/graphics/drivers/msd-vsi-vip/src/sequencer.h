// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_SEQUENCER_H_
#define SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_SEQUENCER_H_

#include "magma_util/macros.h"

class Sequencer {
 public:
  Sequencer(uint32_t first_sequence_number) : next_sequence_number_(first_sequence_number) {}

  uint32_t next_sequence_number() {
    uint32_t sequence_number = next_sequence_number_++;
    // TODO(fxbug.dev/43815): handle sequence number overflow.
    DASSERT(next_sequence_number_ >= sequence_number);
    return sequence_number;
  }

  static constexpr uint32_t kInvalidSequenceNumber = 0;

 private:
  uint32_t next_sequence_number_;
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_SEQUENCER_H_
