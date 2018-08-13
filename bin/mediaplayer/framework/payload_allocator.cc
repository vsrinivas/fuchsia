// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/payload_allocator.h"

#include <cstdlib>
#include <memory>

#include "lib/fxl/logging.h"

namespace media_player {

namespace {

class DefaultAllocator : public PayloadAllocator,
                         public std::enable_shared_from_this<DefaultAllocator> {
 public:
  constexpr DefaultAllocator() {}

  ~DefaultAllocator() {}

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

}  // namespace

// static
std::shared_ptr<PayloadAllocator> PayloadAllocator::CreateDefault() {
  return std::make_shared<DefaultAllocator>();
}

}  // namespace media_player
