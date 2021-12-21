// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/arena.h>
#include <lib/syslog/cpp/macros.h>

#include <vector>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "src/devices/bin/driver_runtime/microbenchmarks/assert.h"

namespace {

// Measure the time taken to allocate and free a |buffer_size|-byte block.
bool ArenaAllocFreeTest(perftest::RepeatState* state, size_t buffer_size) {
  std::string_view tag;
  auto arena = fdf::Arena::Create(0, tag);
  ASSERT_OK(arena.status_value());

  state->DeclareStep("alloc");
  state->DeclareStep("free");
  while (state->KeepRunning()) {
    void* block = arena->Allocate(buffer_size);
    if (!block) {
      return false;
    }
    state->NextStep();
    arena->Free(block);  // Currently a no-op.
  }
  return true;
}

// Measure the time taken to check whether a block is contained in an arena
// which holds |num_blocks|.
bool ArenaContainsTest(perftest::RepeatState* state, uint32_t num_blocks) {
  constexpr size_t kBlockSizeBytes = 0x1000;

  std::string_view tag;
  auto arena = fdf::Arena::Create(0, tag);
  ASSERT_OK(arena.status_value());

  std::vector<uint32_t*> allocated;
  for (uint32_t i = 0; i < num_blocks; i++) {
    auto data = static_cast<uint32_t*>(arena->Allocate(kBlockSizeBytes));
    allocated.push_back(data);
  }

  uint32_t i = 0;

  while (state->KeepRunning()) {
    FX_CHECK(arena->Contains(allocated[i]));
    i++;
    i %= allocated.size();
  }
  return true;
}

void RegisterTests() {
  static const unsigned kBlockSize[] = {
      32, 64, 1024, 8192, 65536,
  };
  for (auto block_size : kBlockSize) {
    auto alloc_free_name = fbl::StringPrintf("Arena/AllocFree/%ubytes", block_size);
    perftest::RegisterTest(alloc_free_name.c_str(), ArenaAllocFreeTest, block_size);
  }

  static const unsigned kNumBlocks[] = {
      1, 4, 16, 32, 1024,
  };
  for (auto num_blocks : kNumBlocks) {
    auto contains_name = fbl::StringPrintf("Arena/Contains/%ublocks", num_blocks);
    perftest::RegisterTest(contains_name.c_str(), ArenaContainsTest, num_blocks);
  }
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
