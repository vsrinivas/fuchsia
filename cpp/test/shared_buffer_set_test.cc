// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/cpp/shared_buffer_set.h"

#include <limits>

#include "gtest/gtest.h"

namespace mojo {
namespace media {
namespace {

uint32_t CreateNewBuffer(SharedBufferSet* under_test, uint64_t size) {
  uint32_t buffer_id;
  ScopedSharedBufferHandle handle;
  MojoResult result = under_test->CreateNewBuffer(size, &buffer_id, &handle);
  EXPECT_EQ(MOJO_RESULT_OK, result);
  EXPECT_TRUE(handle.is_valid());
  return buffer_id;
}

void AddBuffer(SharedBufferSet* under_test, uint64_t size, uint32_t buffer_id) {
  SharedBuffer buffer(size);
  MojoResult result = under_test->AddBuffer(buffer_id, buffer.handle.Pass());
  EXPECT_EQ(MOJO_RESULT_OK, result);
}

void VerifyBuffer(const SharedBufferSet& under_test,
                  uint32_t buffer_id,
                  uint64_t size) {
  uint8_t* base = reinterpret_cast<uint8_t*>(
      under_test.PtrFromLocator(SharedBufferSet::Locator(buffer_id, 0)));
  EXPECT_NE(nullptr, base);

  for (uint64_t offset = 0; offset < size; ++offset) {
    EXPECT_EQ(SharedBufferSet::Locator(buffer_id, offset),
              under_test.LocatorFromPtr(base + offset));
    EXPECT_EQ(base + offset, under_test.PtrFromLocator(
                                 SharedBufferSet::Locator(buffer_id, offset)));
  }
}

// Tests SharedBufferSet::CreateNewBuffer.
TEST(SharedBufferSetTest, CreateNewBuffer) {
  SharedBufferSet under_test;
  uint32_t buffer_id = CreateNewBuffer(&under_test, 1000);
  VerifyBuffer(under_test, buffer_id, 1000);
}

// Tests SharedBufferSet::AddBuffer.
TEST(SharedBufferSetTest, AddBuffer) {
  SharedBufferSet under_test;
  AddBuffer(&under_test, 1000, 0);
  VerifyBuffer(under_test, 0, 1000);
}

// Tests offset/ptr conversion with multiple buffers.
TEST(SharedBufferSetTest, ManyBuffers) {
  SharedBufferSet under_test;
  AddBuffer(&under_test, 1000, 0);
  AddBuffer(&under_test, 2000, 1);
  AddBuffer(&under_test, 3000, 2);
  AddBuffer(&under_test, 4000, 3);
  VerifyBuffer(under_test, 0, 1000);
  VerifyBuffer(under_test, 1, 2000);
  VerifyBuffer(under_test, 2, 3000);
  VerifyBuffer(under_test, 3, 4000);
}

// Tests offset/ptr conversion with removed buffers.
TEST(SharedBufferSetTest, RemovedBuffers) {
  SharedBufferSet under_test;
  AddBuffer(&under_test, 1000, 0);
  AddBuffer(&under_test, 2000, 1);
  AddBuffer(&under_test, 3000, 2);
  AddBuffer(&under_test, 4000, 3);
  under_test.RemoveBuffer(1);
  under_test.RemoveBuffer(3);
  VerifyBuffer(under_test, 0, 1000);
  VerifyBuffer(under_test, 2, 3000);
  AddBuffer(&under_test, 2000, 1);
  AddBuffer(&under_test, 4000, 3);
  under_test.RemoveBuffer(0);
  under_test.RemoveBuffer(2);
  VerifyBuffer(under_test, 1, 2000);
  VerifyBuffer(under_test, 3, 4000);
}

// Tests SharedBufferSet::Validate.
TEST(SharedBufferSetTest, Validate) {
  SharedBufferSet under_test;
  AddBuffer(&under_test, 1000, 0);
  VerifyBuffer(under_test, 0, 1000);
  EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator::Null(), 1));
  EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator(1, 0), 1));
  EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator(2, 0), 1));
  EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator(3, 0), 1));
  EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator(0, 1001), 1));
  for (uint64_t offset = 0; offset < 1000; ++offset) {
    EXPECT_TRUE(under_test.Validate(SharedBufferSet::Locator(0, offset), 1));
    EXPECT_FALSE(under_test.Validate(SharedBufferSet::Locator(0, offset),
                                     1001 - offset));
  }
}

}  // namespace
}  // namespace media
}  // namespace mojo
