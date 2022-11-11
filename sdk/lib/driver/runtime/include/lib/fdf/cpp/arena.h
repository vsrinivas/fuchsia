// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_ARENA_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_ARENA_H_

#include <lib/fdf/arena.h>
#include <lib/fidl/cpp/wire/arena.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>

namespace fdf {

// C++ wrapper for an arena, with RAII semantics. Automatically destroys
// the reference to the runtime arena object when it goes out of scope.
// If there are no more references to the arena, all memory associated with
// the arena will be freed.
//
// |fdf::Arena::Create| will return a reference to a newly created runtime arena object.
// Passing an arena to |fdf::Channel::Write| will create and transfer a new reference to
// that same arena, and does not take ownership of your arena reference.
//
// # Thread safety
//
// This class is thread-unsafe.
//
// # Example
//
//   constexpr fdf_arena_tag_t kTag = 'EXAM';
//   fdf::Arena arena(kTag);
//
//   // Allocate new blocks of memory.
//   void* addr1 = arena.Allocate(arena, 0x1000);
//   void* addr2 = arena.Allocate(arena, 0x2000);
//
//   // Use the allocated memory...
class Arena : public fidl::AnyArena {
 public:
  explicit Arena(fdf_arena_t* arena) : arena_(arena) {}

  // Creates an FDF Arena for allocating memory. This can never fail.
  //
  // |tag| provides a hint to the runtime so that it may be more efficient.
  // For example, adjusting the size of the buffer backing the arena to the expected
  // total size of allocations. It may also be surfaced in debug information.
  explicit Arena(fdf_arena_tag_t tag) : arena_(CreateArenaOrAssert(tag)) {}

  // Creates a FDF arena for allocating memory.
  //
  // |tag| provides a hint to the runtime so that it may be more efficient.
  // For example, adjusting the size of the buffer backing the arena to the expected
  // total size of allocations. It may also be surfaced in debug information.
  //
  // # Errors
  //
  // ZX_ERR_INVALID_ARGS: |options| is any value other than 0.
  //
  // ZX_ERR_NO_MEMORY: Failed due to a lack of memory.
  static zx::result<Arena> Create(uint32_t options, fdf_arena_tag_t tag) {
    fdf_arena_t* arena;
    zx_status_t status = fdf_arena_create(options, tag, &arena);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(Arena(arena));
  }

  // Arena cannot be copied.
  Arena(const Arena& to_copy) = delete;
  Arena& operator=(const Arena& other) = delete;

  // Arena can be moved. Once moved, invoking a method on an instance will
  // yield undefined behavior.
  //
  // Moving the arena does not move any data allocated for |fidl::AnyArena|.
  Arena(Arena&& other) noexcept : Arena(other.release()) {}
  Arena& operator=(Arena&& other) noexcept {
    reset(other.release());
    return *this;
  }

  ~Arena() { close(); }

  // Returns a pointer to allocated memory of size |bytes|. The memory is managed by the arena
  // until it is freed by |Free|, or the arena is destroyed.
  void* Allocate(size_t bytes) const { return fdf_arena_allocate(arena_, bytes); }

  // Hints to the arena that the |ptr| previously allocated by |Allocate| may be reclaimed.
  // Memory is not guaranteed to be reclaimed until the arena is destroyed.
  // Asserts if the memory is not managed by the arena.
  void Free(void* ptr) { fdf_arena_free(arena_, ptr); }

  // Returns whether the memory region corresponding to |ptr| resides entirely within memory
  // managed by the |arena|.
  //
  // # Example
  //
  //   MyStruct* ptr = static_cast<MyStruct*>(arena.Allocate(sizeof(size_t)));
  //   bool contains = arena.Contains<MyStruct>(ptr);
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
    fidl_arena_.Clean();
  }

  fdf_arena_t* release() {
    fdf_arena_t* ret = arena_;
    arena_ = nullptr;
    return ret;
  }

  // Implementation of |fidl::AnyArena|.
  uint8_t* Allocate(size_t item_size, size_t count,
                    void (*destructor_function)(uint8_t* data, size_t count)) override {
    return fidl_arena_.Allocate(item_size, count, destructor_function);
  }

  fdf_arena_t* get() const { return arena_; }

 private:
  struct FidlArena {
   public:
    // Struct used to store the data needed to deallocate an allocation (to call
    // the destructor).
    struct Destructor {
      Destructor(Destructor* next, size_t count, void (*destructor)(uint8_t*, size_t))
          : next(next), count(count), destructor(destructor) {}

      Destructor* const next;
      const size_t count;
      void (*const destructor)(uint8_t*, size_t);
    };

    // Struct used for allocation buffers on the heap.
    struct Block {
     private:
      // Separately define the header of the block, to provide a sizeof.
      struct Header {
        // Next block to deallocate (block allocated before this one).
        Block* next_block;

        // Size of the |data_| portion. Note: although |data_| is declared to have
        // a fixed size, in practice an |Block| might be allocated with a
        // bespoke bigger size to serve a particular big object.
        size_t size;
      };

     public:
      // The size of the block without the data portion.
      static constexpr size_t kBlockHeaderSize = sizeof(Header);

      // In most cases, the size is big enough to only need one allocation.
      // It's also small enough to not use too much heap memory. The actual
      // allocated size for the Block struct will be 16 KiB.
      static constexpr size_t kDefaultBlockSize = 16lu * 1024 - kBlockHeaderSize;

      explicit Block(Block* next_block, size_t size)
          : header_(Header{
                .next_block = next_block,
                .size = size,
            }) {}

      Block* next_block() const { return header_.next_block; }
      uint8_t* data() { return data_; }
      size_t size() const { return header_.size; }

     private:
      Header header_;
      // The usable data.
      alignas(FIDL_ALIGNMENT) uint8_t data_[kDefaultBlockSize];
    };

    void Clean();

    uint8_t* Allocate(size_t item_size, size_t count,
                      void (*destructor_function)(uint8_t* data, size_t count));

   private:
    // Pointer to the next available data.
    uint8_t* next_data_available_ = nullptr;
    // Size of the data available at next_data_available_.
    size_t available_size_ = 0;
    // Linked list of the destructors to call starting with the last allocation.
    Destructor* last_destructor_ = nullptr;
    // Linked list of the blocks used for the allocation.
    Block* last_block_ = nullptr;
  };

  static fdf_arena_t* CreateArenaOrAssert(uint32_t tag) {
    fdf_arena_t* arena;
    zx_status_t status = fdf_arena_create(0, tag, &arena);
    ZX_ASSERT(status == ZX_OK);
    return arena;
  }

  fdf_arena_t* arena_;
  FidlArena fidl_arena_;
};

}  // namespace fdf

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_ARENA_H_
