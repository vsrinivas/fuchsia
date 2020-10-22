// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mmio-ptr/fake.h>
#include <zxtest/zxtest.h>

namespace {

TEST(MmioPtr, LowLevelAPIWrites) {
  uint8_t value8;
  uint16_t value16;
  uint32_t value32;
  uint64_t value64;
  MMIO_PTR uint8_t* value8_ptr = FakeMmioPtr(&value8);
  MMIO_PTR uint16_t* value16_ptr = FakeMmioPtr(&value16);
  MMIO_PTR uint32_t* value32_ptr = FakeMmioPtr(&value32);
  MMIO_PTR uint64_t* value64_ptr = FakeMmioPtr(&value64);

  MmioWrite8(10, value8_ptr);
  MmioWrite16(11, value16_ptr);
  MmioWrite32(12, value32_ptr);
  MmioWrite64(13, value64_ptr);

  ASSERT_EQ(value8, 10);
  ASSERT_EQ(value16, 11);
  ASSERT_EQ(value32, 12);
  ASSERT_EQ(value64, 13);
}

TEST(MmioPtr, LowLevelAPIReads) {
  uint8_t value8 = 10;
  uint16_t value16 = 11;
  uint32_t value32 = 12;
  uint64_t value64 = 13;
  const uint8_t const_value8 = 14;
  const uint16_t const_value16 = 15;
  const uint32_t const_value32 = 16;
  const uint64_t const_value64 = 17;

  MMIO_PTR uint8_t* value8_ptr = FakeMmioPtr(&value8);
  MMIO_PTR uint16_t* value16_ptr = FakeMmioPtr(&value16);
  MMIO_PTR uint32_t* value32_ptr = FakeMmioPtr(&value32);
  MMIO_PTR uint64_t* value64_ptr = FakeMmioPtr(&value64);
  const MMIO_PTR uint8_t* const_value8_ptr = FakeMmioPtr(&const_value8);
  const MMIO_PTR uint16_t* const_value16_ptr = FakeMmioPtr(&const_value16);
  const MMIO_PTR uint32_t* const_value32_ptr = FakeMmioPtr(&const_value32);
  const MMIO_PTR uint64_t* const_value64_ptr = FakeMmioPtr(&const_value64);

  ASSERT_EQ(MmioRead8(value8_ptr), 10);
  ASSERT_EQ(MmioRead16(value16_ptr), 11);
  ASSERT_EQ(MmioRead32(value32_ptr), 12);
  ASSERT_EQ(MmioRead64(value64_ptr), 13);
  ASSERT_EQ(MmioRead8(const_value8_ptr), 14);
  ASSERT_EQ(MmioRead16(const_value16_ptr), 15);
  ASSERT_EQ(MmioRead32(const_value32_ptr), 16);
  ASSERT_EQ(MmioRead64(const_value64_ptr), 17);
}

TEST(MmioPtr, ReadBuffer) {
  const std::array<uint8_t, 256> array = [] {
    std::array<uint8_t, 256> result;
    for (size_t i = 0; i < result.size(); ++i) {
      result[i] = static_cast<uint8_t>(i);
    }
    return result;
  }();

  // Read all but the first and last byte.
  std::array<uint8_t, 256> result = {};
  MMIO_PTR const void* value_ptr = FakeMmioPtr(&array[1]);
  MmioReadBuffer(&result[1], value_ptr, array.size() - 2);

  ASSERT_EQ(result[0], 0u);
  for (size_t i = 1; i < result.size() - 1; ++i) {
    ASSERT_EQ(result[i], i);
  }
  ASSERT_EQ(result[255], 0u);
}

TEST(MmioPtr, WriteBuffer) {
  const std::array<uint8_t, 256> array = [] {
    std::array<uint8_t, 256> result;
    for (size_t i = 0; i < result.size(); ++i) {
      result[i] = static_cast<uint8_t>(i);
    }
    return result;
  }();

  // Read all but the first and last byte.
  std::array<uint8_t, 256> mmio_buffer = {};
  MMIO_PTR void* mmio_ptr = FakeMmioPtr(&mmio_buffer[1]);
  MmioWriteBuffer(mmio_ptr, &array[1], array.size() - 2);

  ASSERT_EQ(mmio_buffer[0], 0u);
  for (size_t i = 1; i < mmio_buffer.size() - 1; ++i) {
    ASSERT_EQ(mmio_buffer[i], i);
  }
  ASSERT_EQ(mmio_buffer[255], 0u);
}

}  // namespace
