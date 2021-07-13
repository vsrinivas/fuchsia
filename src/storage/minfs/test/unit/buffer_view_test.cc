// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/buffer_view.h"

#include <array>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace minfs {
namespace {

using ::testing::_;

constexpr int kArraySize = 100;
constexpr uint8_t kFill = 0x56;
constexpr uint32_t kFill32 = 0x56565656;

TEST(BufferViewTest, UpdatesOnBufferAreReflectedOnView) {
  std::array<uint8_t, kArraySize> array;
  array.fill(kFill);
  constexpr int kIndex = 13;
  constexpr int kLength = 3;
  BufferView<uint32_t> view(BufferPtr::FromMemory(array.data()), kIndex, kLength);
  EXPECT_EQ(kFill32, *view);
  EXPECT_EQ(kFill32, view[2]);
  const uint32_t kData = 0xf00dface;
  reinterpret_cast<uint32_t&>(array[(kIndex + 2) * 4]) = kData;
  EXPECT_EQ(kFill32, *view);
  EXPECT_EQ(kData, view[2]);
}

TEST(BufferViewTest, FlushOnCleanViewIssuesNoFlush) {
  std::array<uint8_t, kArraySize> array;
  array.fill(kFill);
  bool flushed = false;
  BufferView<uint32_t> view(BufferPtr::FromMemory(array.data()), 13, 4, [&](BaseBufferView* view) {
    flushed = true;
    return ZX_OK;
  });

  EXPECT_EQ(view.Flush(), ZX_OK);

  EXPECT_FALSE(flushed);
}

TEST(BufferViewTest, FlushOnDirtyViewIssuesFlush) {
  std::array<uint8_t, kArraySize> array;
  array.fill(kFill);
  bool flushed = false;
  constexpr int kIndex = 13;
  constexpr int kLength = 4;
  BufferView<uint32_t> view(BufferPtr::FromMemory(array.data()), kIndex, kLength,
                            [&](BaseBufferView* view) {
                              flushed = true;
                              return ZX_OK;
                            });
  static const uint32_t kData = 0xfacef00d;
  view.mut_ref(2) = kData;
  EXPECT_EQ(kData, reinterpret_cast<uint32_t&>(array[(kIndex + 2) * 4]));
  EXPECT_TRUE(view.dirty());
  EXPECT_EQ(kData, view[2]);

  EXPECT_EQ(view.Flush(), ZX_OK);

  EXPECT_TRUE(flushed);
}

TEST(BufferViewTest, FlushOnDirtyViewSetsStateToClean) {
  std::array<uint8_t, kArraySize> array;
  array.fill(kFill);
  bool flushed = false;
  BufferView<uint32_t> view(BufferPtr::FromMemory(array.data()), 13, 4, [&](BaseBufferView* view) {
    flushed = true;
    return ZX_OK;
  });
  view.mut_ref(3) = 0x12345678;

  view.set_dirty(false);
  EXPECT_EQ(view.Flush(), ZX_OK);

  EXPECT_FALSE(flushed);
}

TEST(BufferViewTest, Move) {
  std::array<uint8_t, kArraySize> array;
  array.fill(kFill);
  BufferView<uint32_t> view(BufferPtr::FromMemory(array.data()), 13, 3,
                            [&](BaseBufferView* view) { return ZX_OK; });
  static const uint32_t kData = 0xfacef00d;
  view.mut_ref(2) = kData;
  EXPECT_TRUE(view.dirty());

  auto view2 = std::move(view);

  EXPECT_TRUE(view2.dirty());
  EXPECT_TRUE(view2.IsValid());
  EXPECT_EQ(kData, view2[2]);
  view2.set_dirty(false);
}

TEST(BufferViewDeathTest, OutOfRangeReadAsserts) {
  auto test = [] {
    std::array<uint8_t, kArraySize> array;
    BufferView<uint32_t> view(BufferPtr::FromMemory(array.data()), 13, 3);
    view[7];
  };
  ASSERT_DEATH(test(), _);
}

TEST(BufferViewDeathTest, OutOfRangeWriteAsserts) {
  auto test = [] {
    std::array<uint8_t, kArraySize> array;
    BufferView<uint32_t> view(BufferPtr::FromMemory(array.data()), 13, 3,
                              [&](BaseBufferView* view) { return ZX_OK; });
    view.mut_ref(7) = 1;
    view.set_dirty(false);
  };
  ASSERT_DEATH(test(), _);
}

TEST(BufferViewDeathTest, DestructorAssertNonNullFlusher) {
  auto test = [] {
    std::array<uint8_t, kArraySize> array;
    BufferView<uint32_t> view(BufferPtr::FromMemory(array.data()), 13, 3);
    view.mut_ref(0) = 10;
  };
  ASSERT_DEATH(test(), _);
}

TEST(BufferViewDeathTest, DestructorWithDirtyStateAssertsFlushed) {
  auto test = [] {
    std::array<uint8_t, kArraySize> array;
    BufferView<uint32_t> view(BufferPtr::FromMemory(array.data()), 13, 3,
                              [&](BaseBufferView* view) { return ZX_OK; });
    view.mut_ref() = 10;
  };
  ASSERT_DEATH(test(), _);
}

}  // namespace
}  // namespace minfs
