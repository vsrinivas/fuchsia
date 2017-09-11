// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/transport/shared_buffer_set_allocator.h"

namespace media {

SharedBufferSetAllocator::SharedBufferSetAllocator(uint32_t local_map_flags,
                                                   zx_rights_t remote_rights)
    : SharedBufferSet(local_map_flags), remote_rights_(remote_rights) {}

SharedBufferSetAllocator::~SharedBufferSetAllocator() {}

bool SharedBufferSetAllocator::SetFixedBufferSize(uint64_t size) {
  FXL_DCHECK(size != 0);

  fxl::MutexLocker locker(&mutex_);

  FXL_DCHECK(!use_fixed_buffer_) << "SetFixedBufferSize called more than once";
  FXL_DCHECK(buffers_.empty())
      << "SetFixedBufferSize called after AllocateRegion";
  FXL_DCHECK(active_sliced_buffer_id_ == kNullBufferId);

  active_sliced_buffer_id_ = CreateBuffer(false, size);
  if (active_sliced_buffer_id_ == kNullBufferId) {
    return false;
  }

  use_fixed_buffer_ = true;

  return true;
}

void* SharedBufferSetAllocator::AllocateRegion(uint64_t size) {
  FXL_DCHECK(size != 0);

  fxl::MutexLocker locker(&mutex_);

  Locator locator;

  if (size >= kWholeRegionMinimumSize && !use_fixed_buffer_) {
    locator = AllocateWholeRegion(size);
  } else {
    locator = AllocateSlicedRegion(size);
  }

  return PtrFromLocator(locator);
}

void SharedBufferSetAllocator::ReleaseRegion(void* ptr) {
  FXL_DCHECK(ptr != nullptr);

  fxl::MutexLocker locker(&mutex_);

  Locator locator = LocatorFromPtr(ptr);
  if (!locator) {
    return;
  }

  FXL_DCHECK(locator.buffer_id() < buffers_.size());

  if (buffers_[locator.buffer_id()].whole()) {
    ReleaseWholeRegion(locator);
  } else {
    ReleaseSlicedRegion(locator);
  }
}

bool SharedBufferSetAllocator::PollForBufferUpdate(uint32_t* buffer_id_out,
                                                   zx::vmo* handle_out) {
  FXL_DCHECK(buffer_id_out != nullptr);
  FXL_DCHECK(handle_out != nullptr);

  fxl::MutexLocker locker(&mutex_);

  if (buffer_updates_.empty()) {
    return false;
  }

  *buffer_id_out = buffer_updates_.front().buffer_id_;
  *handle_out = std::move(buffer_updates_.front().vmo_);

  buffer_updates_.pop();

  return true;
}

SharedBufferSet::Locator SharedBufferSetAllocator::AllocateWholeRegion(
    uint64_t size) {
  auto lower_bound = free_whole_buffer_ids_by_size_.lower_bound(size);

  for (auto iter = free_whole_buffer_ids_by_size_.begin(); iter != lower_bound;
       ++iter) {
    DeleteBuffer(iter->second);
  }

  free_whole_buffer_ids_by_size_.erase(free_whole_buffer_ids_by_size_.begin(),
                                       lower_bound);

  FXL_DCHECK(lower_bound == free_whole_buffer_ids_by_size_.begin());

  if (lower_bound != free_whole_buffer_ids_by_size_.end()) {
    // Found a free buffer that's large enough. Use it.
    uint32_t buffer_id = lower_bound->second;
    free_whole_buffer_ids_by_size_.erase(lower_bound);
    return Locator(buffer_id, 0);
  }

  // Didn't find a large enough buffer. Create one.
  uint32_t buffer_id = CreateBuffer(true, size);
  if (buffer_id == kNullBufferId) {
    return Locator::Null();
  }

  return Locator(buffer_id, 0);
}

void SharedBufferSetAllocator::ReleaseWholeRegion(const Locator& locator) {
  FXL_DCHECK(locator);
  FXL_DCHECK(locator.buffer_id() < buffers_.size());
  FXL_DCHECK(!buffers_[locator.buffer_id()].allocator_);

  free_whole_buffer_ids_by_size_.insert(
      std::make_pair(buffers_[locator.buffer_id()].size_, locator.buffer_id()));
}

SharedBufferSet::Locator SharedBufferSetAllocator::AllocateSlicedRegion(
    uint64_t size) {
  if (active_sliced_buffer_id_ == kNullBufferId) {
    // No buffer has been established for allocating sliced buffers. Create one.
    FXL_DCHECK(!use_fixed_buffer_);
    active_sliced_buffer_id_ =
        CreateBuffer(false, size * kSlicedBufferInitialSizeMultiplier);
    if (active_sliced_buffer_id_ == kNullBufferId) {
      return Locator::Null();
    }
  }

  // Try allocating from the buffer.
  FXL_DCHECK(buffers_[active_sliced_buffer_id_].allocator_);
  uint64_t offset =
      buffers_[active_sliced_buffer_id_].allocator_->AllocateRegion(size);

  if (offset != FifoAllocator::kNullOffset) {
    // Allocation succeeded.
    return Locator(active_sliced_buffer_id_, offset);
  }

  if (use_fixed_buffer_) {
    // Allocation failed, and we're using a fixed buffer. Fail the request.
    return Locator::Null();
  }

  // Allocation failed - we need a bigger buffer. We either grow the buffer size
  // by a factor of kSlicedBufferGrowMultiplier or use the initial buffer size
  // calculation based on this allocation request, whichever produces the
  // larger buffer.
  //
  // The old buffer will be deleted once all the regions that were allocated
  // from it are released.

  uint64_t new_buffer_size = std::max(
      size * kSlicedBufferInitialSizeMultiplier,
      buffers_[active_sliced_buffer_id_].size_ * kSlicedBufferGrowMultiplier);

  uint32_t buffer_id = CreateBuffer(false, new_buffer_size);
  if (buffer_id == kNullBufferId) {
    return Locator::Null();
  }

  MaybeDeleteSlicedBuffer(active_sliced_buffer_id_);

  active_sliced_buffer_id_ = buffer_id;

  FXL_DCHECK(buffers_[active_sliced_buffer_id_].allocator_);
  offset = buffers_[active_sliced_buffer_id_].allocator_->AllocateRegion(size);
  // The allocation must succeed since the new buffer is at least
  // kSlicedBufferInitialSizeMultiplier times larger than size.
  FXL_DCHECK(offset != FifoAllocator::kNullOffset);

  return Locator(active_sliced_buffer_id_, offset);
}

void SharedBufferSetAllocator::ReleaseSlicedRegion(const Locator& locator) {
  FXL_DCHECK(locator);
  FXL_DCHECK(locator.buffer_id() < buffers_.size());
  FXL_DCHECK(buffers_[locator.buffer_id()].allocator_);

  buffers_[locator.buffer_id()].allocator_->ReleaseRegion(locator.offset());

  // Delete the buffer if it's no longer the active one, and it's fully
  // released.
  if (locator.buffer_id() != active_sliced_buffer_id_) {
    MaybeDeleteSlicedBuffer(locator.buffer_id());
  }
}

uint32_t SharedBufferSetAllocator::CreateBuffer(bool whole, uint64_t size) {
  uint32_t buffer_id;
  zx::vmo vmo;
  zx_status_t status = CreateNewBuffer(size, &buffer_id, remote_rights_, &vmo);
  if (status != ZX_OK) {
    return kNullBufferId;
  }

  if (buffers_.size() <= buffer_id) {
    buffers_.resize(buffer_id + 1);
  }

  Buffer& buffer = buffers_[buffer_id];
  buffer.size_ = size;
  if (!whole) {
    buffer.allocator_.reset(new FifoAllocator(size));
  }

  buffer_updates_.emplace(buffer_id, std::move(vmo));

  return buffer_id;
}

void SharedBufferSetAllocator::DeleteBuffer(uint32_t id) {
  FXL_DCHECK(buffers_.size() > id);
  RemoveBuffer(id);
  buffers_[id].size_ = 0;
  buffers_[id].allocator_.reset();

  buffer_updates_.emplace(id);
}

void SharedBufferSetAllocator::MaybeDeleteSlicedBuffer(uint32_t id) {
  FXL_DCHECK(buffers_[id].allocator_);
  if (!buffers_[id].allocator_->AnyCurrentAllocatedRegions()) {
    DeleteBuffer(id);
  }
}

SharedBufferSetAllocator::Buffer::Buffer() {}

SharedBufferSetAllocator::Buffer::~Buffer() {}

SharedBufferSetAllocator::BufferUpdate::BufferUpdate(uint32_t buffer_id,
                                                     zx::vmo vmo)
    : buffer_id_(buffer_id), vmo_(std::move(vmo)) {}

SharedBufferSetAllocator::BufferUpdate::BufferUpdate(uint32_t buffer_id)
    : buffer_id_(buffer_id) {}

SharedBufferSetAllocator::BufferUpdate::~BufferUpdate() {}

}  // namespace media
