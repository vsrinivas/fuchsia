// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/handle.h"

#include <lib/stdcompat/bit.h>
#include <stdlib.h>

#include <new>

namespace {

// Masks for building a fdf_handle_t value.
//
// handle value bit fields:
//   [31..kHandleGenerationShift]                       : Generation number
//                                                          Masked by kHandleGenerationMask
//   [kHandleGenerationShift-1..kHandleDirIndexShift]   : Index into the handle table directory
//                                                          Masked by kHandleDirIndexMask
//   [kHandleDirIndexShift-1..kHandleIndexShift]        : Index into the handle table
//                                                          Masked by kHandleIndexMask
//   [kHandleIndexShift-1..0]                           : Set to kReservedBitsValue

// First two bits are the zircon handle reserved bits.
constexpr uint32_t kHandleReservedBits = 2;
constexpr uint32_t kHandleReservedBitsMask = (1 << kHandleReservedBits) - 1;
// LSB must be zero.
constexpr uint32_t kHandleReservedBitsValue = 1 < 1;
static_assert(kHandleReservedBitsValue < kHandleReservedBits,
              "kHandleReservedBitsValue does not fit!");

constexpr uint32_t kHandleIndexShift = kHandleReservedBits;
constexpr uint32_t kHandleIndexBits =
    cpp20::bit_width(driver_runtime::HandleTableArena::kHandlesPerTable - 1);
constexpr uint32_t kHandleIndexMask = (driver_runtime::HandleTableArena::kHandlesPerTable - 1)
                                      << kHandleIndexShift;
static_assert(kHandleIndexBits > 0);

constexpr uint32_t kHandleDirIndexShift = kHandleIndexShift + kHandleIndexBits;
constexpr uint32_t kHandleDirIndexBits =
    cpp20::bit_width(driver_runtime::HandleTableArena::kNumTables - 1);
constexpr uint32_t kHandleDirIndexMask = (driver_runtime::HandleTableArena::kNumTables - 1)
                                         << kHandleDirIndexShift;
static_assert(kHandleDirIndexBits > 0);

// All the remaining bits are used to store the handle generation value.
constexpr uint32_t kHandleGenerationMask =
    ~kHandleIndexMask & ~kHandleDirIndexMask & ~kHandleReservedBitsMask;
constexpr uint32_t kHandleGenerationShift = kHandleDirIndexShift + kHandleDirIndexBits;
static_assert(((3 << (kHandleGenerationShift - 1)) & kHandleGenerationMask) ==
                  1 << kHandleGenerationShift,
              "Shift is wrong");
static_assert((kHandleGenerationMask >> kHandleGenerationShift) >= 255,
              "Not enough room for a useful generation count");

static_assert((kHandleReservedBitsMask & kHandleGenerationMask) == 0, "Handle Mask Overlap!");
static_assert((kHandleReservedBitsMask & kHandleIndexMask) == 0, "Handle Mask Overlap!");
static_assert((kHandleGenerationMask & kHandleIndexMask) == 0, "Handle Mask Overlap!");
static_assert((kHandleDirIndexMask & kHandleReservedBitsMask) == 0, "Handle Mask Overlap!");
static_assert((kHandleDirIndexMask & kHandleGenerationMask) == 0, "Handle Mask Overlap!");
static_assert((kHandleDirIndexMask & kHandleIndexMask) == 0, "Handle Mask Overlap!");
static_assert((kHandleReservedBitsMask | kHandleGenerationMask | kHandleDirIndexMask |
               kHandleIndexMask) == 0xffffffffu,
              "Handle masks do not cover all bits!");

// Returns a newly generated handle value.
// |dir_index| is the index into the handle tables directory.
// |index| is the index into the handle table fetched from |dir_index|.
// |old_handle_value| contains the |dir_index| and |index| mixed with the per-handle-lifetime state.
fdf_handle_t new_handle_value(uint32_t dir_index, uint32_t index, fdf_handle_t old_handle_value) {
  // Check that the indexes fit within their assigned bits.
  ZX_ASSERT(((dir_index << kHandleDirIndexShift) & ~kHandleDirIndexMask) == 0);
  ZX_ASSERT(((index << kHandleIndexShift) & ~kHandleIndexMask) == 0);

  uint32_t old_gen = 0;
  if (old_handle_value != 0) {
    // This slot has been used before.
    ZX_ASSERT((old_handle_value & kHandleDirIndexMask) >> kHandleDirIndexShift == dir_index);
    ZX_ASSERT((old_handle_value & kHandleIndexMask) >> kHandleIndexShift == index);
    old_gen = (old_handle_value & kHandleGenerationMask) >> kHandleGenerationShift;
  }
  uint32_t new_gen = (((old_gen + 1) << kHandleGenerationShift) & kHandleGenerationMask);
  return (kHandleReservedBitsValue | index << kHandleIndexShift |
          (dir_index << kHandleDirIndexShift) | new_gen);
}

uint32_t handle_value_to_dir_index(fdf_handle_t handle_value) {
  return (handle_value & kHandleDirIndexMask) >> kHandleDirIndexShift;
}
uint32_t handle_value_to_index(fdf_handle_t handle_value) {
  return (handle_value & kHandleIndexMask) >> kHandleIndexShift;
}

}  // namespace

namespace driver_runtime {

HandleTableArena gHandleTableArena;

fit::nullable<Handle*> HandleTableArena::GetExistingHandle(uint32_t dir_index, uint32_t index) {
  fbl::AutoLock lock(&lock_);
  if (dir_index >= kNumTables) {
    return fit::nullable<Handle*>{};
  }
  if (index >= kHandlesPerTable) {
    return fit::nullable<Handle*>{};
  }
  if (!handle_table_dir_[dir_index]) {
    return fit::nullable<Handle*>{};
  }
  Handle* handle = &(handle_table_dir_[dir_index]->data()[index]);
  return fit::nullable<Handle*>(handle->object() ? handle : nullptr);
}

Handle* HandleTableArena::AllocHandleMemoryLocked(uint32_t* out_dir_index, uint32_t* out_index) {
  // Check if there are any free handles we can re-use.
  // The handle internals will be initialized later.
  if (!free_handles_.is_empty()) {
    auto handle = free_handles_.pop_front();
    *out_dir_index = handle_value_to_dir_index(handle->handle_value());
    *out_index = handle_value_to_index(handle->handle_value());
    return handle;
  }
  // No handles left to allocate.
  if (dir_index_ >= kNumTables) {
    return nullptr;
  }
  // If |dir_index_| is pointing to null, that means we previously filled
  // up the last handle table and need to allocate a new one.
  if (!handle_table_dir_[dir_index_]) {
    handle_table_dir_[dir_index_] = new std::array<Handle, kHandlesPerTable>();
    ZX_ASSERT(handles_index_ == 0);
  }

  ZX_ASSERT(handles_index_ < kHandlesPerTable);
  Handle* handle = &(handle_table_dir_[dir_index_]->data()[handles_index_]);
  *out_dir_index = dir_index_;
  *out_index = handles_index_;
  handles_index_++;

  // Current table is full.
  if (handles_index_ >= kHandlesPerTable) {
    dir_index_++;
    handles_index_ = 0;
  }
  return handle;
}

Handle* HandleTableArena::Alloc(fdf_handle_t* out_value) {
  fbl::AutoLock lock(&lock_);

  uint32_t dir_index;
  uint32_t index;
  Handle* handle = AllocHandleMemoryLocked(&dir_index, &index);
  if (!handle) {
    return nullptr;
  }
  // The handle should be newly allocated or previously destructed.
  ZX_ASSERT(!handle->object());

  *out_value = new_handle_value(dir_index, index, handle->handle_value());
  num_allocated_++;
  return handle;
}

// static
HandleOwner Handle::Create(fbl::RefPtr<Object> object) {
  fdf_handle_t handle_value;
  Handle* handle = gHandleTableArena.Alloc(&handle_value);
  if (!handle) {
    return nullptr;
  }
  return HandleOwner(new (handle) Handle(std::move(object), handle_value));
}

// static
Handle* Handle::MapValueToHandle(fdf_handle_t handle_value) {
  if (!IsFdfHandle(handle_value)) {
    return nullptr;
  }
  uint32_t dir_index = handle_value_to_dir_index(handle_value);
  uint32_t index = handle_value_to_index(handle_value);
  fit::nullable<Handle*> handle = gHandleTableArena.GetExistingHandle(dir_index, index);
  if (!handle) {
    return nullptr;
  }
  // Check that the handle value matches the stored value.
  // If it is different it likely means an already deleted handle is being accessed.
  return handle_value == (*handle)->handle_value() ? *handle : nullptr;
}

// static
bool Handle::IsFdfHandle(zx_handle_t handle_value) {
  return ((handle_value & FDF_HANDLE_FIXED_BITS_MASK) == kHandleReservedBitsValue) ||
         (handle_value == FDF_HANDLE_INVALID);
}

}  // namespace driver_runtime
