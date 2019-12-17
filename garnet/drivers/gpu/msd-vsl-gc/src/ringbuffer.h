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

  // Replaces the value stored in the ringbuffer at offset |dwords_before_tail| with |value|.
  // Returns false if |dwords_before_tail| is zero, or does not point to a currently stored
  // value in the ringbuffer.
  bool Overwrite32(uint32_t dwords_before_tail, uint32_t value);

  // Returns the position corresponding to negative |offset| from the current tail.
  uint32_t SubtractOffset(uint32_t offset);

  friend class RingbufferTest;
};

#endif  // RINGBUFFER_H
