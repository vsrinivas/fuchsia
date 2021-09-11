// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/shared-memory.h"

#include <zircon/errors.h>

#include <limits>
#include <random>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sanitizer/asan_interface.h>

namespace fuzzing {
namespace {

using ::testing::Eq;
using ::testing::Ne;

// Test fixtures.

constexpr size_t kCapacity = 0x1000;

// Helper function to create a deterministically pseudorandom integer type.
template <typename T>
T Pick() {
  static std::mt19937_64 prng;
  return static_cast<T>(prng() & std::numeric_limits<T>::max());
}

// Helper function to create an array of deterministically pseudorandom integer types.
template <typename T>
void PickArray(T* out, size_t out_len) {
  for (size_t i = 0; i < out_len; ++i) {
    out[i] = Pick<T>();
  }
}

// Helper function to create a vector of deterministically pseudorandom integer types.
template <typename T = uint8_t>
std::vector<T> PickVector(size_t size) {
  std::vector<T> v;
  v.reserve(size);
  for (size_t i = 0; i < size; ++i) {
    v.push_back(Pick<T>());
  }
  return v;
}

// These macros test memory poisoning when AddressSanitizer is available.
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#define EXPECT_POISONED(addr, size) EXPECT_NE(__asan_region_is_poisoned(addr, size), nullptr)
#define EXPECT_UNPOISONED(addr, size) EXPECT_EQ(__asan_region_is_poisoned(addr, size), nullptr)
#else
#define EXPECT_POISONED(addr, size)
#define EXPECT_UNPOISONED(addr, size)
#endif  // __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)

// Unit tests.

TEST(SharedMemoryTest, Reserve) {
  SharedMemory shmem;

  shmem.Reserve(kCapacity);
  EXPECT_TRUE(shmem.is_mapped());
  EXPECT_GE(shmem.capacity(), kCapacity);
  auto buffer = shmem.Share();
  EXPECT_TRUE(buffer.vmo.is_valid());
  EXPECT_GE(buffer.size, shmem.capacity());

  // Can recreate
  shmem.Reserve(kCapacity * 2);
  EXPECT_TRUE(shmem.is_mapped());
  EXPECT_GE(shmem.capacity(), kCapacity * 2);
  buffer = shmem.Share();
  EXPECT_TRUE(buffer.vmo.is_valid());
  EXPECT_GE(buffer.size, shmem.capacity());
}

TEST(SharedMemoryTest, Mirror) {
  SharedMemory shmem;
  uint8_t region[kCapacity * 2];

  shmem.Mirror(region, sizeof(region));
  EXPECT_TRUE(shmem.is_mapped());
  EXPECT_GE(shmem.capacity(), sizeof(region));
  auto buffer = shmem.Share();
  EXPECT_TRUE(buffer.vmo.is_valid());
  EXPECT_GE(buffer.size, shmem.capacity());

  // Can recreate
  shmem.Mirror(&region[kCapacity], kCapacity * sizeof(region[0]));
  EXPECT_TRUE(shmem.is_mapped());
  EXPECT_GE(shmem.capacity(), kCapacity);
  buffer = shmem.Share();
  EXPECT_TRUE(buffer.vmo.is_valid());
  EXPECT_GE(buffer.size, shmem.capacity());
}

TEST(SharedMemoryTest, Link) {
  SharedMemory shmem;

  Buffer buffer1;
  buffer1.size = kCapacity;
  EXPECT_EQ(zx::vmo::create(buffer1.size, 0, &buffer1.vmo), ZX_OK);
  shmem.LinkMirrored(std::move(buffer1));
  EXPECT_TRUE(shmem.is_mapped());
  EXPECT_GE(shmem.capacity(), kCapacity);

  // Can remap.
  Buffer buffer2;
  buffer2.size = kCapacity * 2;
  EXPECT_EQ(zx::vmo::create(buffer2.size, 0, &buffer2.vmo), ZX_OK);
  shmem.LinkMirrored(std::move(buffer2));
  EXPECT_TRUE(shmem.is_mapped());
  EXPECT_GE(shmem.capacity(), kCapacity * 2);
}

TEST(SharedMemoryTest, Resize) {
  SharedMemory shmem;
  SharedMemory other;

  shmem.Reserve(kCapacity / 2);
  auto capacity = shmem.capacity();
  other.LinkReserved(shmem.Share());

  EXPECT_EQ(shmem.size(), 0u);
  EXPECT_EQ(other.size(), 0u);

  shmem.Resize(1);
  EXPECT_EQ(shmem.size(), 1u);
  EXPECT_EQ(other.size(), 1u);

  shmem.Resize(capacity - 1);
  EXPECT_EQ(shmem.size(), capacity - 1);
  EXPECT_EQ(other.size(), capacity - 1);

  shmem.Resize(capacity);
  EXPECT_EQ(shmem.size(), capacity);
  EXPECT_EQ(other.size(), capacity);
}

TEST(SharedMemoryTest, Write) {
  SharedMemory shmem;

  shmem.Reserve(kCapacity);
  auto expected = PickVector(shmem.capacity());
  size_t half = expected.size() / 2;

  // Can write in chunks, and before sharing.
  EXPECT_EQ(shmem.size(), 0u);
  shmem.Write(expected.data(), half);
  EXPECT_EQ(shmem.size(), half);

  // Or by bytes, and after sharing
  auto buffer = shmem.Share();
  SharedMemory other;
  other.LinkReserved(std::move(buffer));
  auto* data = expected.data();
  for (size_t i = half; i < expected.size(); ++i) {
    shmem.Write(data[i]);
  }

  // Either way, the contents make it.
  EXPECT_EQ(other.size(), expected.size());
  std::vector<uint8_t> actual(other.data(), other.data() + other.size());
  EXPECT_THAT(actual, Eq(expected));
}

TEST(SharedMemoryTest, Update) {
  SharedMemory shmem;
  auto expected = PickVector(kCapacity);

  // Valid
  shmem.Mirror(expected.data(), expected.size());
  EXPECT_EQ(shmem.size(), kCapacity);
  auto buffer = shmem.Share();

  SharedMemory other;
  other.LinkMirrored(std::move(buffer));
  EXPECT_EQ(other.size(), expected.size());
  std::vector<uint8_t> actual(other.data(), other.data() + other.size());
  EXPECT_THAT(actual, Eq(expected));

  //  Change source data, but don't update. Uses |cksum| to verify |expected| did in fact change.
  auto* begin = expected.data();
  auto* end = begin + expected.size();
  auto cksum = std::accumulate(begin, end, 0, std::bit_xor<>());
  PickArray(expected.data(), expected.size());
  EXPECT_NE(cksum, std::accumulate(begin, end, 0, std::bit_xor<>()));
  actual = std::vector<uint8_t>(other.data(), other.data() + other.size());
  EXPECT_THAT(actual, Ne(expected));

  //  Now update
  shmem.Update();
  actual = std::vector<uint8_t>(other.data(), other.data() + other.size());
  EXPECT_THAT(actual, Eq(expected));
}

TEST(SharedMemoryTest, Clear) {
  SharedMemory shmem;
  auto expected = PickVector(kCapacity);

  shmem.Reserve(kCapacity);
  shmem.Write(expected.data(), expected.size());
  auto buffer = shmem.Share();

  SharedMemory other;
  other.LinkReserved(std::move(buffer));
  std::vector<uint8_t> actual(other.data(), other.data() + other.size());
  EXPECT_THAT(actual, Eq(expected));

  // Valid
  shmem.Clear();
  EXPECT_EQ(shmem.size(), 0u);

  // Can write after clearing.
  expected = PickVector(kCapacity);
  shmem.Write(expected.data(), expected.size());
  actual = std::vector<uint8_t>(other.data(), other.data() + other.size());
  EXPECT_THAT(actual, Eq(expected));
}

TEST(SharedMemoryTest, SetPoisoning) {
  SharedMemory src, dst;
  src.Reserve(kCapacity);
  auto buffer = src.Share();
  dst.LinkReserved(std::move(buffer));
  EXPECT_UNPOISONED(dst.data(), dst.capacity());

  dst.SetPoisoning(true);
  EXPECT_POISONED(dst.data(), dst.capacity());

  auto v = PickVector(kCapacity / 2);
  src.Write(v.data(), v.size());
  EXPECT_EQ(dst.size(), v.size());
  EXPECT_UNPOISONED(dst.data(), dst.size());
  EXPECT_POISONED(dst.data(), dst.capacity());

  dst.SetPoisoning(false);
  EXPECT_UNPOISONED(dst.data(), dst.capacity());
}

}  // namespace
}  // namespace fuzzing
