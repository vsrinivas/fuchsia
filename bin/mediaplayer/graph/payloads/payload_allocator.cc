// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/graph/payloads/payload_allocator.h"

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
  fbl::RefPtr<PayloadBuffer> AllocatePayloadBuffer(uint64_t size) override;
};

fbl::RefPtr<PayloadBuffer> DefaultAllocator::AllocatePayloadBuffer(
    uint64_t size) {
  FXL_DCHECK(size > 0);
  // TODO: Once we have C++17, std::aligned_alloc should work.
  // |aligned_alloc| requires the size to the aligned.
  return PayloadBuffer::Create(size,
                               aligned_alloc(PayloadBuffer::kByteAlignment,
                                             PayloadBuffer::AlignUp(size)),
                               [](PayloadBuffer* payload_buffer) {
                                 FXL_DCHECK(payload_buffer);
                                 std::free(payload_buffer->data());
                                 // The |PayloadBuffer| deletes itself.
                               });
}

}  // namespace

// static
std::shared_ptr<PayloadAllocator> PayloadAllocator::CreateDefault() {
  return std::make_shared<DefaultAllocator>();
}

}  // namespace media_player
