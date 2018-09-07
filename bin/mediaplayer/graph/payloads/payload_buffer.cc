// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/graph/payloads/payload_buffer.h"

#include <cstdlib>
#include <memory>
#include "lib/fxl/logging.h"

namespace media_player {

// static
fbl::RefPtr<PayloadBuffer> PayloadBuffer::Create(uint64_t size, void* data,
                                                 Recycler recycler) {
  return fbl::AdoptRef(new PayloadBuffer(size, data, std::move(recycler)));
}

PayloadBuffer::PayloadBuffer(uint64_t size, void* data, Recycler recycler)
    : size_(size), data_(data), recycler_(std::move(recycler)) {
  FXL_DCHECK(size_ != 0);
  FXL_DCHECK(data_ != nullptr);
  FXL_DCHECK(recycler_);
}

PayloadBuffer::~PayloadBuffer() {
  FXL_DCHECK(!recycler_) << "PayloadBuffers must delete themselves.";
}

void PayloadBuffer::BeforeRecycling(Action action) {
  FXL_DCHECK(!before_recycling_) << "BeforeRecycling may only be called once.";
  before_recycling_ = std::move(action);
}

void PayloadBuffer::fbl_recycle() {
  FXL_DCHECK(recycler_ != nullptr);

  if (before_recycling_) {
    before_recycling_(this);
    // It seems cleaner to release this function now so anything it captures
    // is released before the recycler runs.
    before_recycling_ = nullptr;
  }

  recycler_(this);
  // This tells the destructor that deletion is being done properly.
  recycler_ = nullptr;

  delete this;
}

}  // namespace media_player
