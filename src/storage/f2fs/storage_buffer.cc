// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

#ifdef __Fuchsia__
StorageBuffer::StorageBuffer(Bcache *bc, size_t blocks, uint32_t block_size, std::string_view label,
                             uint32_t allocation_unit)
    : max_blocks_(bc->Maxblk()), allocation_unit_(allocation_unit) {
  ZX_DEBUG_ASSERT(allocation_unit >= 1 && allocation_unit <= blocks);
  ZX_ASSERT(buffer_.Initialize(bc, blocks, block_size, label.data()) == ZX_OK);
  Init();
}
#else   // __Fuchsia__
StorageBuffer::StorageBuffer(Bcache *bc, size_t blocks, uint32_t block_size, std::string_view label,
                             uint32_t allocation_unit)
    : max_blocks_(bc->Maxblk()), allocation_unit_(1), buffer_(blocks, block_size) {
  Init();
}
#endif  // __Fuchsia__

void StorageBuffer::Init() {
  std::lock_guard lock(mutex_);
  for (size_t i = 0; i < buffer_.capacity(); i += allocation_unit_) {
    auto key = std::make_unique<VmoBufferKey>(i, buffer_.vmoid());
    free_list_.push_back(std::move(key));
  }
}

zx::result<size_t> StorageBuffer::ReserveWriteOperation(fbl::RefPtr<Page> page, block_t blk_addr) {
  if (blk_addr >= max_blocks_) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  std::lock_guard lock(mutex_);
  // Wait until there is a room in |buffer_|.
  while (free_list_.is_empty()) {
    if (auto wait_result = cvar_.wait_for(mutex_, kWriteTimeOut);
        wait_result == std::cv_status::timeout) {
      FX_LOGS(ERROR) << "[f2fs] Allocating write buffers timeout. ";
      return zx::error(ZX_ERR_TIMED_OUT);
    }
  }

  auto key = free_list_.pop_front();
  storage::Operation op = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = key->GetKey(),
      .dev_offset = blk_addr,
      .length = 1,
  };
  // Copy |page| to |buffer| at |key|.
  std::memcpy(buffer_.Data(op.vmo_offset), page->GetAddress(), page->BlockSize());
  // Here, |operation| can be merged into a previous operation.
  builder_.Add(op, &buffer_);
  pages_.push_back(std::move(page));
  inflight_list_.push_back(std::move(key));
  return zx::ok(pages_.size());
}

zx::result<PageOperations> StorageBuffer::ReserveReadOperations(std::vector<LockedPage> &pages,
                                                                std::vector<block_t> addrs) {
  if (pages.size() != addrs.size()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fs::BufferedOperationsBuilder builder;
  VmoKeyList keys;
  std::vector<fbl::RefPtr<Page>> io_pages;

  uint32_t i = 0, allocate_index = 0;
  for (auto addr : addrs) {
    if (addr != kNullAddr && !pages[i]->IsUptodate()) {
      if (addr != kNewAddr) {
        std::lock_guard lock(mutex_);

        // If addr is invalid, free allocated keys.
        if (addr >= max_blocks_) {
          free_list_.splice(free_list_.end(), keys);
          return zx::error(ZX_ERR_OUT_OF_RANGE);
        }

        if (allocate_index % allocation_unit_ == 0) {
          allocate_index = 0;
          // Wait until there is a room in |buffer_|.
          while (free_list_.is_empty()) {
            if (auto wait_result = cvar_.wait_for(mutex_, kWriteTimeOut);
                wait_result == std::cv_status::timeout) {
              FX_LOGS(ERROR) << "[f2fs] Allocating read buffers timeout. ";
              return zx::error(ZX_ERR_TIMED_OUT);
            }
          }
          keys.push_back(free_list_.pop_front());
        }

        storage::Operation op = {
            .type = storage::OperationType::kRead,
            .vmo_offset = keys.back().GetKey() + allocate_index,
            .dev_offset = addr,
            .length = 1,
        };
        builder.Add(op, &buffer_);
        io_pages.push_back(pages[i].CopyRefPtr());
        ++allocate_index;
      } else {
        // If it is a newly allocated block, just zero it.
        // Refer to VnodeF2fs::GetLockedDataPages().
        pages[i]->ZeroUserSegment(0, kPageSize);
        pages[i]->SetUptodate();
      }
    }
    ++i;
  }
  if (io_pages.empty()) {
    ZX_DEBUG_ASSERT(keys.is_empty());
    return zx::error(ZX_ERR_CANCELED);
  }
  return zx::ok(PageOperations(builder.TakeOperations(), std::move(io_pages), std::move(keys),
                               [this](const PageOperations &op, const zx_status_t io_status) {
                                 ReleaseReadBuffers(op, io_status);
                               }));
}

void StorageBuffer::ReleaseReadBuffers(const PageOperations &operation,
                                       const zx_status_t io_status) {
  if (!operation.IsEmpty()) {
    auto keys = operation.TakeVmoKeys();
    ZX_DEBUG_ASSERT(!keys.is_empty());
    if (io_status == ZX_OK) {
      auto key = keys.begin();
      uint32_t allocate_index = 0;
      for (size_t i = 0; i < operation.GetSize(); ++i) {
        ZX_ASSERT(operation.PopulatePage(buffer_.Data(key->GetKey() + allocate_index), i).is_ok());
        if ((++allocate_index) == allocation_unit_) {
          allocate_index = 0;
          ++key;
        }
      }
    }
    std::lock_guard lock(mutex_);
    // Add vmo buffers of |operation| to |free_list_| to allow waiters to reserve buffer_.
    free_list_.splice(free_list_.end(), keys);
    cvar_.notify_all();
  }
}

void StorageBuffer::ReleaseWriteBuffers(const PageOperations &operation,
                                        const zx_status_t io_status) {
  if (!operation.IsEmpty()) {
    std::lock_guard lock(mutex_);
    auto keys = operation.TakeVmoKeys();
    ZX_DEBUG_ASSERT(!keys.is_empty());
    // Add vmo buffers of |operation| to |free_list_| to allow waiters to reserve buffer_.
    free_list_.splice(free_list_.end(), keys);
    // TODO: When multi-qd is available, consider notify_all().
    cvar_.notify_one();
  }
}

bool StorageBuffer::IsEmpty() {
  fs::SharedLock lock(mutex_);
  return free_list_.is_empty();
}

PageOperations StorageBuffer::TakeWriteOperations() {
  std::lock_guard lock(mutex_);
  return PageOperations(builder_.TakeOperations(), std::move(pages_), std::move(inflight_list_),
                        [this](const PageOperations &op, const zx_status_t io_status) {
                          ReleaseWriteBuffers(op, io_status);
                        });
}

StorageBuffer::~StorageBuffer() {
  {
    std::lock_guard lock(mutex_);
    ZX_DEBUG_ASSERT(pages_.size() == 0);
    [[maybe_unused]] size_t num_keys = 0;
    while (!free_list_.is_empty()) {
      ++num_keys;
      free_list_.pop_front();
    }
    ZX_DEBUG_ASSERT(num_keys == buffer_.capacity() / allocation_unit_);
  }
}

}  // namespace f2fs
