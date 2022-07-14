// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace media_audio {
namespace {

constexpr size_t kVmoSize = 4096;

TEST(MemoryMappedBufferTest, CreateReadOnly) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kVmoSize, 0, &vmo), ZX_OK);

  auto buffer = MemoryMappedBuffer::Create(std::move(vmo), false);
  ASSERT_NE(buffer->start(), nullptr);
  EXPECT_EQ(buffer->size(), kVmoSize);

  // Reading the memory-mapped data should not crash.
  char value = 42;
  ASSERT_EQ(vmo.write(&value, 0, 1), ZX_OK);
  ASSERT_EQ(static_cast<char*>(buffer->start())[0], value);
}

TEST(MemoryMappedBufferTest, CreateWritable) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kVmoSize, 0, &vmo), ZX_OK);

  auto buffer = MemoryMappedBuffer::Create(std::move(vmo), true);
  ASSERT_NE(buffer->start(), nullptr);
  EXPECT_EQ(buffer->size(), kVmoSize);

  // Reading the memory-mapped data should not crash.
  {
    char value = 42;
    ASSERT_EQ(vmo.write(&value, 0, 1), ZX_OK);
    ASSERT_EQ(static_cast<char*>(buffer->start())[0], value);
  }

  // Writing the memory-mapped data should not crash.
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
