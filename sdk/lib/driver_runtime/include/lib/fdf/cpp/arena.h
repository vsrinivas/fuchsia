// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_ARENA_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_ARENA_H_

#include <lib/fdf/arena.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/status.h>

namespace fdf {

// Usage Notes:
//
// C++ wrapper for an arena, with RAII semantics. Automatically frees
// all allocated memory when it goes out of scope.
//
// Example:
//
//   std::string_view tag;
//   auto arena = fdf::Arena::Create(0, tag);
//
//   // Allocate new blocks of memory.
//   void* addr1 = arena.Allocate(arena, 0x1000);
//   void* addr2 = arena.Allocate(arena, 0x2000);
//
//   // Use the allocated memory...
//
class Arena {
 public:
  explicit Arena(fdf_arena_t* arena = nullptr) : arena_(arena) {}

  // |tag| provides a hint to the runtime so that it may be more efficient.
  // For example, adjusting the size of the buffer backing the arena
  // to the expected total size of allocations.
  // It may also be surfaced in debug information.
  static zx::status<Arena> Create(uint32_t options, cpp17::string_view tag) {
    fdf_arena_t* arena;
    fdf_status_t status = fdf_arena_create(options, tag.data(), tag.size(), &arena);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(Arena(arena));
  }

  Arena(const Arena& to_copy) = delete;
  Arena& operator=(const Arena& other) = delete;

  Arena(Arena&& other) noexcept : Arena(other.release()) {}
  Arena& operator=(Arena&& other) noexcept {
    reset(other.release());
    return *this;
  }

  ~Arena() { close(); }

  void* Allocate(size_t bytes) const { return fdf_arena_allocate(arena_, bytes); }
  void Free(void* ptr) { fdf_arena_free(arena_, ptr); }

  // Returns true if the memory region of |ptr| resides entirely within memory
  // managed by the |arena|.
  template <typename T>
  bool Contains(const T* ptr) const {
    return fdf_arena_contains(arena_, ptr, sizeof(T));
  }

  void reset(fdf_arena_t* arena = nullptr) {
    close();
    arena_ = arena;
  }

  void close() {
    if (arena_) {
      fdf_arena_destroy(arena_);
      arena_ = nullptr;
    }
  }

  fdf_arena_t* release() {
    fdf_arena_t* ret = arena_;
    arena_ = nullptr;
    return ret;
  }

  fdf_arena_t* get() const { return arena_; }

 private:
  fdf_arena_t* arena_;
};

}  // namespace fdf

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_ARENA_H_
