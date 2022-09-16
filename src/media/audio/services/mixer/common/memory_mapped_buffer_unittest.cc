// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"

#include <zircon/types.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace media_audio {
namespace {

// Use less than 4096 (the typical page sizes) to test that CONTENT_SIZE is set separately from the
// allocated size.
constexpr size_t kContentSize = 96;

TEST(MemoryMappedBufferTest, FailsBadHandle) {
  zx::vmo vmo;
  auto result = MemoryMappedBuffer::Create(vmo, false);
  ASSERT_FALSE(result.is_ok());
}

TEST(MemoryMappedBufferTest, FailsResizable) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kContentSize, ZX_VMO_RESIZABLE, &vmo), ZX_OK);

  auto result = MemoryMappedBuffer::Create(vmo, false);
  ASSERT_FALSE(result.is_ok());
}

TEST(MemoryMappedBufferTest, FailsNotReadable) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kContentSize, ZX_VMO_DISCARDABLE, &vmo), ZX_OK);
  ASSERT_EQ(vmo.replace(ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY, &vmo), ZX_OK);

  auto result = MemoryMappedBuffer::Create(vmo, false);
  ASSERT_FALSE(result.is_ok());
}

TEST(MemoryMappedBufferTest, FailsNotMappable) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kContentSize, ZX_VMO_DISCARDABLE, &vmo), ZX_OK);
  ASSERT_EQ(vmo.replace(ZX_RIGHT_READ | ZX_RIGHT_GET_PROPERTY, &vmo), ZX_OK);

  auto result = MemoryMappedBuffer::Create(vmo, false);
  ASSERT_FALSE(result.is_ok());
}

TEST(MemoryMappedBufferTest, FailsNotWritable) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kContentSize, ZX_VMO_DISCARDABLE, &vmo), ZX_OK);
  ASSERT_EQ(vmo.replace(ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY, &vmo), ZX_OK);

  auto result = MemoryMappedBuffer::Create(vmo, true);
  ASSERT_FALSE(result.is_ok());
}

TEST(MemoryMappedBufferTest, SuccessReadOnly) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kContentSize, 0, &vmo), ZX_OK);
  ASSERT_EQ(vmo.replace(ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY, &vmo), ZX_OK);

  auto result = MemoryMappedBuffer::Create(vmo, false);
  ASSERT_TRUE(result.is_ok());

  auto buffer = result.value();
  ASSERT_NE(buffer->start(), nullptr);
  EXPECT_EQ(buffer->size(), zx_system_get_page_size());
  EXPECT_EQ(buffer->content_size(), kContentSize);

  // Reading the memory-mapped data should not crash.
  char data;
  ASSERT_EQ(vmo.read(&data, 0, 1), ZX_OK);
}

TEST(MemoryMappedBufferTest, SuccessWritable) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kContentSize, 0, &vmo), ZX_OK);

  auto result = MemoryMappedBuffer::Create(vmo, true);
  ASSERT_TRUE(result.is_ok());

  auto buffer = result.value();
  ASSERT_NE(buffer->start(), nullptr);
  EXPECT_EQ(buffer->size(), zx_system_get_page_size());
  EXPECT_EQ(buffer->content_size(), kContentSize);

  // Writing the memory-mapped data should not crash.
  {
    char value = 42;
    ASSERT_EQ(vmo.write(&value, 0, 1), ZX_OK);
    ASSERT_EQ(static_cast<char*>(buffer->start())[0], value);
  }

  // Reading the memory-mapped data should not crash.
  {
    char value = 123;
    static_cast<char*>(buffer->start())[0] = value;
    char data;
    ASSERT_EQ(vmo.read(&data, 0, 1), ZX_OK);
    EXPECT_EQ(data, value);
  }
}

}  // namespace
}  // namespace media_audio
