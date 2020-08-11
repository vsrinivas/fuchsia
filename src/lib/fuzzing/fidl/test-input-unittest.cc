// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-input.h"

#include <zircon/errors.h>

#include <gtest/gtest.h>

namespace fuzzing {

TEST(TestInputTest, Create) {
  TestInput input;

  // Bad length
  EXPECT_EQ(input.Create(0x1000), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(input.data(), nullptr);

  // Valid
  EXPECT_EQ(input.Create(), ZX_OK);
  EXPECT_NE(input.data(), nullptr);

  size_t size;
  EXPECT_EQ(input.vmo().get_size(&size), ZX_OK);
  EXPECT_EQ(size, TestInput::kVmoSize);

  // Can recreate
  const uint8_t *prev = input.data();
  EXPECT_EQ(input.Create(), ZX_OK);
  EXPECT_NE(input.data(), prev);
}

TEST(TestInputTest, Link) {
  TestInput input;

  // Bad VMO.
  zx::vmo vmo;
  EXPECT_EQ(input.Link(vmo), ZX_ERR_BAD_HANDLE);
  EXPECT_EQ(input.data(), nullptr);

  // Too small
  size_t size = TestInput::kVmoSize;
  EXPECT_EQ(zx::vmo::create(size / 2, 0, &vmo), ZX_OK);
  EXPECT_EQ(input.Link(vmo), ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_EQ(input.data(), nullptr);

  // Bad length
  EXPECT_EQ(zx::vmo::create(size, 0, &vmo), ZX_OK);
  EXPECT_EQ(input.Link(vmo, size - 1), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(input.data(), nullptr);

  // Valid
  EXPECT_EQ(input.Link(vmo), ZX_OK);
  EXPECT_NE(input.data(), nullptr);

  // Can remap
  const uint8_t *prev = input.data();
  EXPECT_EQ(input.Link(vmo), ZX_OK);
  EXPECT_NE(input.data(), prev);
}

TEST(TestInputTest, Write) {
  TestInput input;
  EXPECT_EQ(input.size(), 0u);

  // No VMO is mapped.
  uint8_t data[0x1000];
  EXPECT_EQ(input.Write(data, sizeof(data)), ZX_ERR_BAD_STATE);
  EXPECT_EQ(input.size(), 0u);

  EXPECT_EQ(input.Create(), ZX_OK);
  EXPECT_EQ(input.size(), 0u);

  // Valid
  EXPECT_EQ(input.Write(data, sizeof(data)), ZX_OK);
  EXPECT_EQ(input.size(), sizeof(data));

  // Capped at kMaxInputSize.
  while (input.size() < TestInput::kMaxInputSize) {
    size_t len = std::min(sizeof(data), TestInput::kMaxInputSize - input.size());
    EXPECT_EQ(input.Write(data, len), ZX_OK);
  }
  EXPECT_EQ(input.size(), TestInput::kMaxInputSize);
  EXPECT_EQ(input.Write(data, sizeof(data)), ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_EQ(input.size(), TestInput::kMaxInputSize);
}

TEST(TestInputTest, Clear) {
  TestInput input;
  EXPECT_EQ(input.size(), 0u);

  // No VMO is mapped.
  EXPECT_EQ(input.Clear(), ZX_ERR_BAD_STATE);
  EXPECT_EQ(input.size(), 0u);

  EXPECT_EQ(input.Create(), ZX_OK);
  EXPECT_EQ(input.size(), 0u);

  uint8_t data[0x1000];
  EXPECT_EQ(input.Write(data, sizeof(data)), ZX_OK);
  EXPECT_EQ(input.size(), sizeof(data));

  // Valid
  EXPECT_EQ(input.Clear(), ZX_OK);
  EXPECT_EQ(input.size(), 0u);
}

}  // namespace fuzzing
