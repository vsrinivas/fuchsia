// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_HEAP_CMPCTMALLOC_TESTS_PAGE_MANAGER_H_
#define ZIRCON_KERNEL_LIB_HEAP_CMPCTMALLOC_TESTS_PAGE_MANAGER_H_

#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/limits.h>

#include <map>
#include <memory>

// PageManager backs our dummy heap implementation, managing blocks of
// pages allocated by the OS.
//
// Its implementation is complicated by |cmpct_trim()|, which allows for the
// freeing of strict heads and tails of the blocks; otherwise, we could just
// implement |heap_page_alloc()| and |heap_page_free()| as thin wrappers around
// |new[]| and |delete[]|, respectively.
//
class PageManager {
 public:
  PageManager() = default;
  ~PageManager() = default;

  void* AllocatePages(size_t num_pages);
  void FreePages(void* p, size_t num_pages);

 private:
 struct PageAlignedDeleter {
    void operator()(char* ptr) const {
          operator delete[](ptr, std::align_val_t{ZX_PAGE_SIZE});
    }
  };
  // Represents an OS-allocated block of pages that tracks the contiguous
  // subset of pages of it still available for use.
  //
  // Newly constructred objects or newly freed subregions within it are
  // expected to have their contents filled with Block::kCleanFill.
  struct Block {
    Block(size_t num_pages)
        : size_bytes(num_pages * ZX_PAGE_SIZE),
          available_bytes(size_bytes),
          contents(new(std::align_val_t{ZX_PAGE_SIZE}) char[size_bytes], PageAlignedDeleter()),
          available_start(contents.get()) {
      memset(contents.get(), kCleanFill, size_bytes);
    }

    ~Block() {
      if (contents == nullptr) {  // Block was 'moved from'.
        return;
      }
      // A block might be destroyed with a non-trivial available region still
      // in use. We can only make guarantees that its complement as remained
      // unallocated since being freed.
      ZX_ASSERT(RangeIsCleanFilled(contents.get(), available_start));
      ZX_ASSERT(RangeIsCleanFilled(available_end() + 1, contents.get() + size_bytes));
    }

    Block(Block&& other) = default;
    Block& operator=(Block&& other) = default;

    char* available_end() const {
      ZX_ASSERT(contents != nullptr);
      return available_start + available_bytes;
    }

    static bool RangeIsCleanFilled(char* begin, char* end) {
      for (char* it = begin; it < end; it++) {
        if (*it != kCleanFill) {
          return false;
        }
      }
      return true;
    }

    // Frees the given number of pages from the head of the available subregion.
    void FreeHead(size_t num_pages) {
      ZX_ASSERT(contents != nullptr);
      size_t size = num_pages * ZX_PAGE_SIZE;
      ZX_ASSERT(size > 0 && size <= available_bytes);
      memset(available_start, kCleanFill, size);
      available_start += size;
      available_bytes -= size;
    }

    // Frees the given number of pages from the tail of the available subregion.
    void FreeTail(size_t num_pages) {
      ZX_ASSERT(contents != nullptr);
      size_t size = num_pages * ZX_PAGE_SIZE;
      ZX_ASSERT(size > 0 && size <= available_bytes);
      memset(available_end() - size, kCleanFill, size);
      available_bytes -= size;
    }

    // See Block documentation.
    static constexpr char kCleanFill = 0x41;
    // The total size of the block, in bytes.
    size_t size_bytes;
    // The size of the available subregion, in bytes.
    size_t available_bytes;
    // The contents of the block.
    std::unique_ptr<char[], PageAlignedDeleter> contents;
    // The start of the available region.
    char* available_start;
  };

  // Maps the left-most pointer in an OS-allocated block of pages to
  // information about that block.
  // The choice of comparator ensures that std::map::lower_bound() returns
  // a pointer to the block that contains it, if one exists.
  using BlockMap = std::map<char*, Block, std::greater<char*>>;
  BlockMap blocks_;
};

#endif  // ZIRCON_KERNEL_LIB_HEAP_CMPCTMALLOC_TESTS_PAGE_MANAGER_H_
