// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_stress.h"

#include <lib/zx/time.h>

#include <future>
#include <initializer_list>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "memory_range.h"

namespace hwstress {
namespace {

TEST(WritePattern, Simple) {
  // Write out a simple pattern to memory.
  std::unique_ptr<MemoryRange> memory =
      MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kCached).value();
  WritePattern(memory->span(), 0x55555555'55555555ul);

  // Ensure it was written correctly.
  for (size_t i = 0; i < ZX_PAGE_SIZE; i++) {
    EXPECT_EQ(memory->bytes()[i], 0x55);
  }
}

TEST(WritePattern, EndianCheck) {
  // Write out a pattern to memory.
  std::unique_ptr<MemoryRange> memory =
      MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kCached).value();
  WritePattern(memory->span(), 0x00112233'44556677ul);

  // Ensure that bytes were written in the correct (big-endian) order.
  for (size_t i = 0; i < ZX_PAGE_SIZE; i += 8) {
    EXPECT_EQ(memory->bytes()[i + 0], 0x00);
    EXPECT_EQ(memory->bytes()[i + 1], 0x11);
    EXPECT_EQ(memory->bytes()[i + 2], 0x22);
    EXPECT_EQ(memory->bytes()[i + 3], 0x33);
    EXPECT_EQ(memory->bytes()[i + 4], 0x44);
    EXPECT_EQ(memory->bytes()[i + 5], 0x55);
    EXPECT_EQ(memory->bytes()[i + 6], 0x66);
    EXPECT_EQ(memory->bytes()[i + 7], 0x77);
  }
}

TEST(VerifyPattern, Simple) {
  // Write out a pattern to memory.
  std::unique_ptr<MemoryRange> memory =
      MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kCached).value();

  // All correct.
  memset(memory->bytes(), 0x55, ZX_PAGE_SIZE);
  EXPECT_EQ(std::nullopt, VerifyPattern(memory->span(), 0x55555555'55555555));

  // Incorrect bytes at various locations.
  for (int bad_byte_index :
       std::initializer_list<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, ZX_PAGE_SIZE - 1}) {
    memset(memory->bytes(), 0x55, ZX_PAGE_SIZE);
    memory->bytes()[bad_byte_index] = 0x0;
    EXPECT_TRUE(VerifyPattern(memory->span(), 0x55555555'55555555).has_value());
  }
}

TEST(Memory, GenerateMemoryWorkloads) {
  // Generate workloads, and exercise each on 4096 bytes of RAM.
  std::unique_ptr<MemoryRange> memory =
      MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kCached).value();
  for (const MemoryWorkload& workload : GenerateMemoryWorkloads()) {
    workload.exec(memory.get());
  }
}

TEST(Memory, CatchBitFlip) {
  // Ensure that at least one workload can catch a bitflip triggered by another thread.
  std::unique_ptr<MemoryRange> memory =
      MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kCached).value();

  // Create a thread which just keeps setting a bit to "1".
  std::atomic<bool> stop = false;
  std::thread bad_memory_thread = std::thread([&memory, &stop]() {
    auto* bad_byte = reinterpret_cast<std::atomic<uint8_t>*>(&memory->bytes()[ZX_PAGE_SIZE / 2]);
    while (!stop) {
      bad_byte->fetch_or(0x1);
    }
  });

  // Ensure we catch it with a panic.
  std::vector<MemoryWorkload> workloads = GenerateMemoryWorkloads();
  EXPECT_DEATH(({
                 while (true) {
                   for (const MemoryWorkload& workload : workloads) {
                     workload.exec(memory.get());
                   }
                 }
               }),
               "Found memory error");

  // Tell the other thread to stop.
  stop = true;
  bad_memory_thread.join();
}

TEST(Memory, StressMemory) {
  // Exercise the main StressMemory function for a tiny amount of time.
  EXPECT_TRUE(StressMemory(zx::msec(1)));
}

}  // namespace
}  // namespace hwstress
