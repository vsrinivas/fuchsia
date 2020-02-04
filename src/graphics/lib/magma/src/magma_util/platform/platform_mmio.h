// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_MMIO_H
#define PLATFORM_MMIO_H

#include <stdint.h>

#include <memory>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace magma {

// Created from a PlatformPciDevice.
class PlatformMmio {
 public:
  PlatformMmio(void* addr, uint64_t size) : addr_(addr), size_(size) {}

  virtual ~PlatformMmio() {}

  enum CachePolicy {
    CACHE_POLICY_CACHED = 0,
    CACHE_POLICY_UNCACHED = 1,
    CACHE_POLICY_UNCACHED_DEVICE = 2,
    CACHE_POLICY_WRITE_COMBINING = 3,
  };

  // Gets the physical address of the MMIO. Not implemented for MMIOs from PCI devices.
  virtual uint64_t physical_address() = 0;

  void Write32(uint64_t offset, uint32_t val) {
    DASSERT(offset < size());
    DASSERT((offset & 0x3) == 0);
    *reinterpret_cast<volatile uint32_t*>(addr(offset)) = val;
  }

  uint32_t Read32(uint64_t offset) {
    DASSERT(offset < size());
    DASSERT((offset & 0x3) == 0);
    return *reinterpret_cast<volatile uint32_t*>(addr(offset));
  }

  void Write64(uint64_t offset, uint64_t val) {
    DASSERT(offset < size());
    DASSERT((offset & 0x7) == 0);
    *reinterpret_cast<volatile uint64_t*>(addr(offset)) = val;
  }

  uint64_t Read64(uint64_t offset) {
    DASSERT(offset < size());
    DASSERT((offset & 0x7) == 0);
    return *reinterpret_cast<volatile uint64_t*>(addr(offset));
  }

  // Posting reads serve to ensure that a previous bus write at the same address has completed.
  uint32_t PostingRead32(uint64_t offset) { return Read32(offset); }
  uint64_t PostingRead64(uint64_t offset) { return Read64(offset); }

  void* addr() { return addr_; }
  uint64_t size() { return size_; }

 private:
  void* addr(uint64_t offset) {
    DASSERT(offset < size_);
    return reinterpret_cast<uint8_t*>(addr_) + offset;
  }

  void* addr_;
  uint64_t size_;

  DISALLOW_COPY_AND_ASSIGN(PlatformMmio);
};

}  // namespace magma

#endif  // PLATFORM_MMIO_H
