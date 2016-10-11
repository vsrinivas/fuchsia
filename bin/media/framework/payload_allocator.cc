// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/payload_allocator.h"

#include <cstdlib>

#include "lib/ftl/logging.h"

namespace mojo {
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
  FTL_DCHECK(size > 0);
  return std::malloc(static_cast<size_t>(size));
}

void DefaultAllocator::ReleasePayloadBuffer(void* buffer) {
  FTL_DCHECK(buffer);
  std::free(buffer);
}

static constexpr DefaultAllocator default_allocator;

}  // namespace

// static
PayloadAllocator* PayloadAllocator::GetDefault() {
  return const_cast<DefaultAllocator*>(&default_allocator);
}

}  // namespace media
}  // namespace mojo
