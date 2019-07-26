// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <id_allocator/id_allocator.h>
#include <lib/zircon-internal/debug.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#define ZXDEBUG 0

namespace id_allocator {

namespace {
constexpr size_t kMaxId = SIZE_MAX;
constexpr int8_t kLevelBits = 6;
constexpr size_t kLevelMask = static_cast<size_t>((1 << kLevelBits) - 1);
constexpr size_t kMaxChildren = static_cast<size_t>(1 << kLevelBits);

constexpr int8_t log64_int8_ceil(size_t id_count) {
  size_t max = 1 << kLevelBits;
  int8_t i = 1;

  while (max < id_count) {
    max = max << kLevelBits;
    i++;
  }

  return i;
}
// Returns number of levels needed to map id_count ids
// id_count <= 64, levels == 1
// id_count <= 64 * 64, levels = 2
constexpr int8_t GetLevels(size_t id_count) { return log64_int8_ceil(id_count); }

}  // namespace

size_t IdAllocator::LevelBitCount(int8_t level) const {
  if (level) {
    size_t pad = 1;
    pad = (pad << (kLevelBits * level)) - 1;
    return ((id_count_ + pad) >> (kLevelBits * level));
  }

  return id_count_;
}

size_t IdAllocator::LevelBitCountRounded(int8_t level) const {
  return fbl::round_up(LevelBitCount(level), kMaxChildren);
}

bool IdAllocator::SetBitAt(int8_t level, size_t bit) {
  ZX_ASSERT(bit < levels_[level].size());
  levels_[level].SetOne(bit);
  bit = bit & ~kLevelMask;
  return levels_[level].Get(bit, bit + kMaxChildren);
}

void IdAllocator::ClearBitAt(int8_t level, size_t bit) {
  ZX_ASSERT(bit < levels_[level].size());
  levels_[level].ClearOne(bit);
}

size_t IdAllocator::FindFirstUnset(int8_t level, size_t base_index) const {
  size_t first_zero;

  ZX_ASSERT((base_index & kLevelMask) == 0);
  if (!levels_[level].Get(base_index, base_index + kMaxChildren, &first_zero)) {
    return first_zero;
  }
  return kMaxId;
}

size_t IdAllocator::Find() const {
  size_t id = 0, index;
  for (int8_t level = static_cast<int8_t>(level_count_ - 1); level >= 0; level--) {
    id = id << kLevelBits;
    index = FindFirstUnset(level, id);
    if (index == kMaxId) {
      // if all ids are busy then all bits at root level will be set.
      ZX_ASSERT(level == level_count_ - 1);
      return kMaxId;
    }
    id = id | index;
  }

  return id;
}

void IdAllocator::MarkBusyInternal(size_t id, int8_t level) {
  ZX_ASSERT(level < level_count_);
  size_t index = id;
  bool all_children_busy;
  for (; level < level_count_; level++) {
    all_children_busy = SetBitAt(level, index);
    if (all_children_busy == false) {
      break;
    }
    index = index >> kLevelBits;
  }
}

void IdAllocator::MarkBusy(size_t id) { MarkBusyInternal(id, 0); }

void IdAllocator::MarkFreeInternal(size_t id, int8_t level) {
  size_t index = id;
  for (; level < level_count_; level++) {
    if (level != 0) {
      if (levels_[level].GetOne(index) == false) {
        break;
      }
    }

    ClearBitAt(level, index);
    index = index >> kLevelBits;
  }
}

void IdAllocator::MarkFree(size_t id) { MarkFreeInternal(id, 0); }

zx_status_t IdAllocator::Allocate(size_t* out) {
  size_t id;

  if ((id = Find()) >= id_count_) {
    xprintf("Found an id:%ld level:%d id_count:%ld\n", id, level_count_, id_count_);
    return ZX_ERR_NO_RESOURCES;
  }
  xprintf("Setting id:%ld level:%d id_count:%ld\n", id, level_count_, id_count_);
  ZX_ASSERT(id < id_count_);

  ZX_ASSERT(!IsBusy(id));
  MarkBusy(id);

  *out = id;
  return ZX_OK;
}

bool IdAllocator::IsBusy(size_t id) const {
  if (id >= id_count_) {
    return false;
  }
  return levels_[0].GetOne(id);
}

zx_status_t IdAllocator::MarkAllocated(size_t id) {
  if (id >= id_count_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (IsBusy(id)) {
    return ZX_ERR_BAD_STATE;
  }
  xprintf("Setting id:%ld level:%d id_count:%ld\n", id, level_count_, id_count_);
  MarkBusy(id);
  return ZX_OK;
}

zx_status_t IdAllocator::Free(size_t id) {
  if (id >= id_count_) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (!IsBusy(id)) {
    return ZX_ERR_BAD_STATE;
  }
  xprintf("Freeing id:%ld level:%d id_count:%ld\n", id, level_count_, id_count_);
  MarkFree(id);
  return ZX_OK;
}

// Unallocatable bits are those sets of bits that are out of range for current
// value of id_count_. These bits are set so that searching for free bits becomes
// faster.
// For simplicity we iterate over each unallocatable bit. This isn't a lot given
// there can be at most 63 unallocatable bits for a level
// TODO(auradkar): Use range set
void IdAllocator::MarkUnallocatable(int8_t level) {
  size_t start = LevelBitCount(level);
  size_t end = LevelBitCountRounded(level);

  for (size_t i = start; i < end; i++) {
    MarkBusyInternal(i, level);
  }
}

// For simplicity we iterate over each unallocatable bit. This isn't a lot given
// there can be at most 63 unallocatable bits for a level
// TODO(auradkar): range clear
void IdAllocator::MarkAllAllocatable(int8_t level) {
  size_t start = LevelBitCount(level);
  size_t end = LevelBitCountRounded(level);

  for (size_t i = start; i < end; i++) {
    MarkFreeInternal(i, level);
  }
}

// Grow should not destroy existing data on failure. We leave successfully
// grown levels levels as is on failure. They are marked busy and
// unallocatable anyway
zx_status_t IdAllocator::GrowInternal(size_t id_count) {
  zx_status_t status;
  xprintf("Growing from %ld to %ld\n", id_count_, id_count);
  id_count_ = id_count;
  level_count_ = GetLevels(id_count_);
  for (int8_t level = 0; level < level_count_; level++) {
    size_t lsize = LevelBitCountRounded(level);
    // We are adding a new level
    if (levels_[level].size() == 0) {
      xprintf("Resetting level %d to size %ld\n", level, lsize);
      status = levels_[level].Reset(lsize);
      // If we are adding a new parent level and if all the children
      // bits are busy, we set the parent bit as busy.
      // When we add a new level, only one parent (at index 0) will
      // inherit all the existing children.
      if ((level > 0) && (levels_[level - 1].Get(0, kMaxChildren))) {
        levels_[level].SetOne(0);
      }
    } else {
      // We are extending an existing level
      xprintf("Growing level %d from %ld to %ld\n", level, levels_[level].size(), lsize);
      status = levels_[level].Grow(lsize);
    }

    if (status != ZX_OK) {
      return status;
    }
    MarkUnallocatable(level);
  }
  return ZX_OK;
}

zx_status_t IdAllocator::Grow(size_t id_count) {
  size_t old_id_count = id_count_;
  if (id_count == id_count_) {
    return ZX_OK;
  }

  if (id_count < id_count_) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (id_count >= kMaxId) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  for (int8_t level = 0; level < level_count_; level++) {
    MarkAllAllocatable(level);
  }
  zx_status_t status = GrowInternal(id_count);
  if (status != ZX_OK) {
    // This should never fail. We should not have freed any
    // existing resources
    ZX_ASSERT(GrowInternal(old_id_count) == ZX_OK);
  }
  return status;
}

zx_status_t IdAllocator::Shrink(size_t id_count) {
  if (id_count == id_count_) {
    return ZX_OK;
  }

  if ((id_count > id_count_)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  int8_t old_level_count = GetLevels(id_count_);
  id_count_ = id_count;
  level_count_ = GetLevels(id_count);

  for (int8_t level = 0; level < old_level_count; level++) {
    // Free any level that was allocated but is not needed anymore
    if (level >= level_count_) {
      ZX_ASSERT(levels_[level].Reset(0) == ZX_OK);
      continue;
    }
    size_t lsize = LevelBitCountRounded(level);
    ZX_ASSERT(levels_[level].Shrink(lsize) == ZX_OK);
    MarkUnallocatable(level);
  }

  return ZX_OK;
}

zx_status_t IdAllocator::Reset(size_t id_count) {
  if ((id_count >= kMaxId)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx_status_t status;
  if (id_count < id_count_) {
    status = Shrink(id_count);
  } else {
    status = Grow(id_count);
  }

  if (status != ZX_OK) {
    return status;
  }

  for (int8_t level = 0; level < level_count_; level++) {
    ZX_ASSERT(levels_[level].Reset(levels_[level].size()) == ZX_OK);
    MarkUnallocatable(level);
  }

  return ZX_OK;
}

void IdAllocator::Dump() const {
  xprintf("kMaxLevels:%d id_count:%lu level_count:%d\n", kMaxLevels, id_count_, level_count_);
  for (int8_t level = static_cast<int8_t>(level_count_ - 1); level >= 0; level--) {
    xprintf("\nlevel: %d\n", level);
    for (size_t index = 0; index < levels_[level].size(); index++) {
      xprintf("%d", levels_[level].GetOne(index) ? 1 : 0);
    }
  }
  xprintf("\n");
}

zx_status_t IdAllocator::Create(size_t id_count, std::unique_ptr<IdAllocator>* ida_out) {
  if (id_count >= kMaxId) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AllocChecker ac;
  IdAllocator* ida = new (&ac) IdAllocator;

  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  std::unique_ptr<IdAllocator> idap(ida);
  zx_status_t status;

  for (int8_t i = 0; i < kMaxLevels; i++) {
    if ((status = idap->levels_[i].Reset(0)) != ZX_OK) {
      return status;
    }
  }

  status = idap->GrowInternal(id_count);

  if (status == ZX_OK) {
    *ida_out = std::move(idap);
  }

  return status;
}

}  // namespace id_allocator
