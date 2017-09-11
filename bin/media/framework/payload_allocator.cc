// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/framework/payload_allocator.h"

#include <cstdlib>

#include "lib/fxl/logging.h"

namespace media {

namespace {

class DefaultAllocator : public PayloadAllocator {
 public:
  constexpr DefaultAllocator() {}

  // PayloadAllocator implementation.
  void* AllocatePayloadBuffer(size_t size) override;

  void ReleasePayloadBuffer(void* buffer) override;
};

void* DefaultAllocator::AllocatePayloadBuffer(size_t size) {
  FXL_DCHECK(size > 0);
  return std::malloc(size);
}

void DefaultAllocator::ReleasePayloadBuffer(void* buffer) {
  FXL_DCHECK(buffer);
  std::free(buffer);
}

static constexpr DefaultAllocator default_allocator;

}  // namespace

// static
PayloadAllocator* PayloadAllocator::GetDefault() {
  return const_cast<DefaultAllocator*>(&default_allocator);
}

}  // namespace media
