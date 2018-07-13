// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <fbl/algorithm.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define KB(n) ((n)*1024ull)
#define MB(n) (KB(n) * 1024ull)
#define GB(n) (MB(n) * 1024ull)

class Mmu : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    vmar_size_ = GB(1);
    if (zx::vmar::root_self()->allocate(
            0, vmar_size_,
            ZX_VM_FLAG_CAN_MAP_READ | ZX_VM_FLAG_CAN_MAP_SPECIFIC, &vmar_,
            &vmar_base_) != ZX_OK) {
      state.SkipWithError("Failed to create vmar");
      return;
    }

    if (zx::vmo::create(MB(4), 0, &vmo_) != ZX_OK) {
      state.SkipWithError("Failed to create vmo");
      return;
    }
  }

  void TearDown(benchmark::State& state) override {
    if (vmar_) {
        vmar_.destroy();
    }
  }

 protected:
  // Cyclically maps the first chunk_size bytes of |vmo_| into the |length| bytes of vmar_,
  // starting from offset 0.   Mapping is done |chunk_size| bytes at a time.  |chunk_size|
  // and |length| must be multiples of PAGE_SIZE.
  // As a precondition, |vmar_| should be empty.
  zx_status_t MapInChunks(size_t chunk_size, size_t length, bool force_into_mmu);

  zx::vmar vmar_;
  zx::vmo vmo_;
  uintptr_t vmar_base_;
  size_t vmar_size_;
};

zx_status_t Mmu::MapInChunks(size_t chunk_size, size_t length, bool force_into_mmu) {
  zx_status_t status;
  uint32_t flags = ZX_VM_FLAG_SPECIFIC | ZX_VM_FLAG_PERM_READ;
  if (force_into_mmu) {
    flags |= ZX_VM_FLAG_MAP_RANGE;
  }

  for (size_t offset = 0; offset < length; offset += chunk_size) {
    uintptr_t addr;
    size_t len = fbl::min(chunk_size, length - offset);
    status = vmar_.map(offset, vmo_, 0, len, flags, &addr);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

// This attempts to measure the amount of time it takes to add and remove mappings through
// the kernel VM layer and the arch MMU layer.
BENCHMARK_F(Mmu, MapUnmap)(benchmark::State& state) {
  while (state.KeepRunning()) {
    // Map just under a large page at a time, to force small pages.  We map many
    // pages at once still, to exercise any optimizations the kernel may perform
    // for small contiguous mappings.
    zx_status_t status = MapInChunks(511 * KB(4), vmar_size_, /* force_into_mmu */ true);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to map");
      return;
    }

    status = vmar_.unmap(vmar_base_, vmar_size_);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to unmap");
      return;
    }
  }
  // Report number of pages
  state.SetItemsProcessed((vmar_size_ / KB(4)) * state.iterations());
}

// This attempts to measure the amount of time it takes to add mappings in
// the kernel VM layer, page fault the mappings into the arch MMU layer, and
// then remove the mappings from both.
BENCHMARK_F(Mmu, MapUnmapWithFaults)(benchmark::State& state) {
  constexpr size_t kSize = MB(128);
  while (state.KeepRunning()) {
    // Map just under a large page at a time, to force small pages.  We map many
    // pages at once still, to exercise any optimizations the kernel may perform
    // for small contiguous mappings.
    zx_status_t status = MapInChunks(511 * KB(4), kSize, /* force_into_mmu */ false);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to map");
      return;
    }

    // Read fault everything in
    auto p = reinterpret_cast<volatile uint8_t*>(vmar_base_);
    for (size_t offset = 0; offset < kSize; offset += PAGE_SIZE) {
      p[offset];
    }

    status = vmar_.unmap(vmar_base_, kSize);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to unmap");
      return;
    }
  }
  // Report number of pages
  state.SetItemsProcessed((kSize / KB(4)) * state.iterations());
}
