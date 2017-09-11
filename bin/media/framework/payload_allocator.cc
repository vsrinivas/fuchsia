// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/framework/payload_allocator.h"

#include <cstdlib>
#include <memory>

#include "lib/fxl/logging.h"

namespace media {

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

static const std::shared_ptr<PayloadAllocator> default_allocator =
    std::make_shared<DefaultAllocator>();

}  // namespace

// static
std::shared_ptr<PayloadAllocator> PayloadAllocator::GetDefault() {
  return default_allocator;
}

}  // namespace media
