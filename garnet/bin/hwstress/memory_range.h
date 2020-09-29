// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_MEMORY_RANGE_H_
#define GARNET_BIN_HWSTRESS_MEMORY_RANGE_H_

#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/span.h>

#include "compiler.h"

namespace hwstress {

// Determines if memory should have CPU caches enabled on it.
enum class CacheMode {
  kCached,
  kUncached,
};

// A MemoryRange tracks a (potentially large) range of memory mapped into the address space.
class MemoryRange {
 public:
  ~MemoryRange();

  // Create and map a memory range of the given size.
  //
  // Size must be aligned to ZX_PAGE_SIZE.
  static zx::status<std::unique_ptr<MemoryRange>> Create(uint64_t size, CacheMode mode);

  // Create a MemoryRange from the given pre-mapped VMO.
  MemoryRange(zx::vmo vmo, uint8_t* addr, uint64_t size, CacheMode mode);

  // Get the cache mode of the memory.
  CacheMode cache() const { return cache_mode_; }

  // Get the memory range as a fbl::Span.
  fbl::Span<uint8_t> span() const { return fbl::Span<uint8_t>(addr_, size_); }

  // Get a raw pointer to the memory represented in bytes.
  uint8_t* bytes() const ASSUME_ALIGNED(ZX_PAGE_SIZE) { return addr_; }
  uint64_t size_bytes() const { return size_; }

  // Get a raw pointer to the memory represented as 64-bit words.
  uint64_t* words() const ASSUME_ALIGNED(ZX_PAGE_SIZE) {
    return reinterpret_cast<uint64_t*>(addr_);
  }
  uint64_t size_words() const { return size_ / sizeof(uint64_t); }

  // CPU cache operations, via the kernel.
  void CleanCache();
  void CleanInvalidateCache();

  // Return the underlying vmo.
  const zx::vmo& vmo() const { return vmo_; }

 private:
  // Perform the given cache operation on the entire VMO range.
  void DoCacheOp(uint32_t operation);

  zx::vmo vmo_;
  uint8_t* addr_;
  uint64_t size_;
  CacheMode cache_mode_;
};

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_MEMORY_RANGE_H_
