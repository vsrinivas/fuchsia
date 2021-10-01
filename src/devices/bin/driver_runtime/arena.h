// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_ARENA_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_ARENA_H_

#include <lib/fdf/types.h>
#include <lib/fidl/llcpp/arena.h>

#include <map>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/ref_counted.h>

struct fdf_arena : public fbl::RefCounted<fdf_arena> {
 public:
  ~fdf_arena();

  fdf_arena(fdf_arena&& to_move) = delete;
  fdf_arena(fdf_arena& to_copy) = delete;

  // fdf_arena_t implementation
  static fdf_status_t Create(uint32_t options, const char* tag, fdf_arena** out_arena);
  void* Allocate(size_t bytes);
  bool Contains(const void* data, size_t num_bytes);
  void* Free(void* data);
  void Destroy();

 private:
  // Size of the buffer allocated on construction of the arena.
  static constexpr size_t kInitialBufferSize = 4ull * 1024;

  struct ExtraBlock;
  using ExtraBlockNode = fbl::SinglyLinkedListable<ExtraBlock*>;

  // Struct used to have more allocation buffers on the heap (when the initial
  // buffer is full).
  struct ExtraBlock : public ExtraBlockNode {
   public:
    // The actual allocated size for the ExtraBlock struct will be 16 KiB.
    static constexpr size_t kExtraSize = 16ull * 1024 - FIDL_ALIGN(sizeof(ExtraBlockNode));

    uint8_t* data() { return data_; }

   private:
    // The usable data.
    alignas(FIDL_ALIGNMENT) uint8_t data_[kExtraSize];
  };

  fdf_arena() = default;

  // Returns a pointer to the newest allocated buffer.
  uint8_t* NewestBufferLocked() __TA_REQUIRES(&lock_) {
    return extra_blocks_.is_empty() ? initial_buffer_ : extra_blocks_.front().data();
  }

  fbl::Mutex lock_;
  // Pointer to the next available data.
  uint8_t* next_data_available_ __TA_GUARDED(&lock_) = initial_buffer_;
  // Size of the data available at next_data_available_.
  size_t available_size_ __TA_GUARDED(&lock_) = kInitialBufferSize;
  // Linked list of the extra blocks used for the allocation.
  fbl::SinglyLinkedList<ExtraBlock*> extra_blocks_ __TA_GUARDED(&lock_);
  // Map from the address of the allocated data block (i.e. ExtraBlock::data())
  // to the allocated size.
  std::map<uintptr_t, size_t> allocated_ranges_ __TA_GUARDED(&lock_);

  // Initial buffer allocated for the arena.
  alignas(FIDL_ALIGNMENT) uint8_t initial_buffer_[kInitialBufferSize];
};

#endif  // SRC_DEVICES_BIN_DRIVER_RUNTIME_ARENA_H_
