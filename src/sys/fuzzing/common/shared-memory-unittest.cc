// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/shared-memory.h"

#include <zircon/errors.h>

#include <limits>
#include <random>

#include <gtest/gtest.h>
#include <sanitizer/asan_interface.h>

namespace fuzzing {
namespace {

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

  EXPECT_EQ(shmem.Reserve(kCapacity), ZX_OK);
  zx::vmo vmo;
  EXPECT_EQ(shmem.Share(&vmo), ZX_OK);
  EXPECT_TRUE(vmo.is_valid());
  size_t size;
  EXPECT_EQ(vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size)), ZX_OK);
  EXPECT_EQ(size, 0U);

  size_t mapped_size;
  EXPECT_EQ(vmo.get_size(&mapped_size), ZX_OK);
  EXPECT_POISONED(shmem.data(), mapped_size);

  // Can recreate.
  EXPECT_EQ(shmem.Reserve(kCapacity * 2), ZX_OK);
  EXPECT_EQ(shmem.Share(&vmo), ZX_OK);
  EXPECT_TRUE(vmo.is_valid());
  EXPECT_EQ(vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size)), ZX_OK);
  EXPECT_EQ(size, 0U);

  EXPECT_EQ(vmo.get_size(&mapped_size), ZX_OK);
  EXPECT_POISONED(shmem.data(), mapped_size);
}

TEST(SharedMemoryTest, Mirror) {
  SharedMemory shmem;
  uint8_t region[kCapacity * 2];

  EXPECT_EQ(shmem.Mirror(region, sizeof(region)), ZX_OK);
  EXPECT_EQ(shmem.size(), sizeof(region));
  zx::vmo vmo;
  EXPECT_EQ(shmem.Share(&vmo), ZX_OK);
  EXPECT_TRUE(vmo.is_valid());

  size_t size;
  EXPECT_EQ(vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size)), ZX_OK);
  EXPECT_EQ(size, shmem.size());
  EXPECT_UNPOISONED(shmem.data(), shmem.size());

  // Can recreate. Pick a size that should have at least one ASAN alignment boundary between it and
  // the next page boundary.
  size = kCapacity + (zx_system_get_page_size() / 2);
  EXPECT_EQ(shmem.Mirror(region, size * sizeof(region[0])), ZX_OK);
  EXPECT_EQ(shmem.size(), size);
  EXPECT_EQ(shmem.Share(&vmo), ZX_OK);
  EXPECT_TRUE(vmo.is_valid());
  EXPECT_EQ(vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size)), ZX_OK);
  EXPECT_EQ(size, shmem.size());

  EXPECT_EQ(vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size)), ZX_OK);
  EXPECT_EQ(size, shmem.size());
  EXPECT_UNPOISONED(shmem.data(), shmem.size());

  size_t mapped_size;
  EXPECT_EQ(vmo.get_size(&mapped_size), ZX_OK);
  EXPECT_POISONED(shmem.data() + size, mapped_size - size);
}

TEST(SharedMemoryTest, Link) {
  SharedMemory shmem;

  zx::vmo vmo;
  EXPECT_EQ(zx::vmo::create(kCapacity, 0, &vmo), ZX_OK);
  size_t size = kCapacity;
  EXPECT_EQ(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size)), ZX_OK);
  EXPECT_EQ(shmem.Link(std::move(vmo)), ZX_OK);
  EXPECT_GE(shmem.size(), kCapacity);

  // Can remap.
  EXPECT_EQ(zx::vmo::create(kCapacity * 2, 0, &vmo), ZX_OK);
  size = kCapacity / 2;
  EXPECT_EQ(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size)), ZX_OK);
  EXPECT_EQ(shmem.Link(std::move(vmo)), ZX_OK);
  EXPECT_GE(shmem.size(), kCapacity / 2);
}

TEST(SharedMemoryTest, Write) {
  SharedMemory shmem;

  EXPECT_EQ(shmem.Reserve(kCapacity), ZX_OK);
  auto bytes = PickVector(shmem.size());
  size_t half = bytes.size() / 2;

  // Can write before sharing...
  EXPECT_EQ(shmem.size(), 0u);
  EXPECT_EQ(shmem.Write(bytes.data(), half), ZX_OK);
  EXPECT_EQ(shmem.size(), half);

  zx::vmo vmo;
  EXPECT_EQ(shmem.Share(&vmo), ZX_OK);
  SharedMemory other;
  EXPECT_EQ(other.Link(std::move(vmo)), ZX_OK);

  auto actual = std::vector<uint8_t>(other.data(), other.data() + other.size());
  auto expected = std::vector(bytes.data(), bytes.data() + half);
  EXPECT_EQ(actual, expected);

  // ...and after sharing
  EXPECT_EQ(shmem.Write(bytes.data(), bytes.size()), ZX_OK);
  EXPECT_EQ(other.Read(), ZX_OK);

  actual = std::vector<uint8_t>(other.data(), other.data() + other.size());
  expected = std::move(bytes);
  EXPECT_EQ(actual, expected);
}

TEST(SharedMemoryTest, Update) {
  SharedMemory shmem;
  auto expected = PickVector(kCapacity);

  // Valid
  EXPECT_EQ(shmem.Mirror(expected.data(), expected.size()), ZX_OK);
  EXPECT_EQ(shmem.size(), kCapacity);
  zx::vmo vmo;
  EXPECT_EQ(shmem.Share(&vmo), ZX_OK);

  SharedMemory other;
  EXPECT_EQ(other.Link(std::move(vmo)), ZX_OK);
  EXPECT_EQ(other.size(), expected.size());
  auto actual = std::vector<uint8_t>(other.data(), other.data() + other.size());
  EXPECT_EQ(actual, expected);

  //  Change source data, but don't update. Uses |cksum| to verify |expected| did in fact change.
  auto* begin = expected.data();
  auto* end = begin + expected.size();
  auto cksum = std::accumulate(begin, end, 0, std::bit_xor<>());
  PickArray(expected.data(), expected.size());
  EXPECT_NE(cksum, std::accumulate(begin, end, 0, std::bit_xor<>()));
  actual = std::vector<uint8_t>(other.data(), other.data() + other.size());
  EXPECT_NE(actual, expected);

  //  Now update
  shmem.Update();
  actual = std::vector<uint8_t>(other.data(), other.data() + other.size());
  EXPECT_EQ(actual, expected);
}

}  // namespace
}  // namespace fuzzing
