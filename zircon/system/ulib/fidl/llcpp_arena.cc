// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/arena.h>

#include <iostream>

namespace fidl {

AnyArena::~AnyArena() { Clean(); }

void AnyArena::Clean() {
  // Call all the destructors (starting with the last allocated object).
  // Because we only work with views, the destructors only close handles.
  while (last_destructor_ != nullptr) {
    last_destructor_->destructor(
        reinterpret_cast<uint8_t*>(last_destructor_) + FIDL_ALIGN(sizeof(Destructor)),
        last_destructor_->count);
    last_destructor_ = last_destructor_->next;
  }
  // Deletes all the extra blocks.
  while (last_extra_block_ != nullptr) {
    ExtraBlock* to_be_deleted = last_extra_block_;
    last_extra_block_ = last_extra_block_->next_block();
    delete[] reinterpret_cast<char*>(to_be_deleted);
  }
}

uint8_t* AnyArena::Allocate(size_t size, size_t count,
                            void (*destructor_function)(uint8_t*, size_t)) {
  // Total size needed for the allocation (the header used for the deallocation and the data).
  size_t block_size = FIDL_ALIGN(size * count);
  // Checks that the multiplication didn't overflow.
  ZX_DEBUG_ASSERT((count == 0) || (block_size >= size));
  if (destructor_function != nullptr) {
    block_size += FIDL_ALIGN(sizeof(Destructor));
  }
  if (available_size_ < block_size) {
    // The data doesn't fit within the current block => allocate a new block.
    // Note: the data available at the end of the current block is lost forever (until the
    // deallocation of the arena).
    available_size_ = (block_size > ExtraBlock::kExtraSize) ? block_size : ExtraBlock::kExtraSize;
    size_t extra_block_size = available_size_ + FIDL_ALIGN(sizeof(ExtraBlock*));
    last_extra_block_ = new (new uint8_t[extra_block_size]) ExtraBlock(last_extra_block_);
    next_data_available_ = last_extra_block_->data();
  }
  // At this point we have enough space within the current block (either because there was enough
  // space within the existing block or because we allocate a new block).
  uint8_t* data = next_data_available_;
  next_data_available_ += block_size;
  available_size_ -= block_size;
  if (destructor_function != nullptr) {
    // Creates the data used to deallocate this allocation.
    last_destructor_ = new (data) Destructor(last_destructor_, count, destructor_function);
    return data + FIDL_ALIGN(sizeof(Destructor));
  }
  return data;
}

}  // namespace fidl
