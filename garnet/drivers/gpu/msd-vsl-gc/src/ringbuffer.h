// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <magma_util/ringbuffer.h>

#include "gpu_mapping.h"

class Ringbuffer : public magma::Ringbuffer<GpuMapping> {
 public:
  Ringbuffer(std::unique_ptr<MsdVslBuffer>&& buffer, uint32_t start_offset)
      : magma::Ringbuffer<GpuMapping>(std::move(buffer), start_offset) {}

  // Returns whether |offset| points to a currently stored value in the ringbuffer.
  bool IsOffsetPopulated(uint32_t offset);

  // Replaces the value stored in the ringbuffer at |offset| with |value|.
  // Returns false if |offset| does not point to a currently stored
  // value in the ringbuffer.
  bool Overwrite32(uint32_t offset, uint32_t value);

  // Returns the position corresponding to negative |offset| from the current tail.
  uint32_t SubtractOffset(uint32_t offset);

  // Advances the ringbuffer tail so that the next write(s) totalling |want_bytes| will be
  // contiguous.
  // Returns whether the requested number of contiguous bytes were available,
  // and any required ringbuffer tail adjustment was made.
  // If false, the caller should wait for an existing event to be removed
  // from the ringbuffer before trying again.
  bool ReserveContiguous(uint32_t want_bytes);

  friend class RingbufferTest;
  friend class RingbufferTest_OffsetPopulatedHeadBeforeTail_Test;
  friend class RingbufferTest_OffsetPopulatedTailBeforeHead_Test;
};

#endif  // RINGBUFFER_H
