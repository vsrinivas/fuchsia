// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fuzzing/cpp/traits.h>
#include <lib/zx/handle.h>
#include <zircon/types.h>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace fuzzing {
namespace {

constexpr size_t size0 = 0;
constexpr size_t size1 = 1;
constexpr size_t size2 = 2;
constexpr size_t size8 = 8;

template <typename T1>
std::pair<T1, size_t> DoSizedAlloc(size_t size) {
  std::vector<uint8_t> data(size, 0);
  ::fuzzing::FuzzInput src(data.data(), size);
  T1 ret = Allocate<T1>{}(&src, &size);
  return std::pair<T1, size_t>(std::move(ret), size);
}

template <typename T1, typename T2 = T1>
std::pair<T1, size_t> DoAlloc() {
  return DoSizedAlloc<T1>(sizeof(T2));
}

TEST(TraitsTest, StaticMinSizesMatch) {
  EXPECT_EQ(MinSize<bool>(), sizeof(bool));
  EXPECT_EQ(MinSize<uint8_t>(), sizeof(uint8_t));
  EXPECT_EQ(MinSize<uint16_t>(), sizeof(uint16_t));
  EXPECT_EQ(MinSize<uint32_t>(), sizeof(uint32_t));
  EXPECT_EQ(MinSize<uint64_t>(), sizeof(uint64_t));
  EXPECT_EQ(MinSize<int8_t>(), sizeof(int8_t));
  EXPECT_EQ(MinSize<int16_t>(), sizeof(int16_t));
  EXPECT_EQ(MinSize<int32_t>(), sizeof(int32_t));
  EXPECT_EQ(MinSize<int64_t>(), sizeof(int64_t));
  EXPECT_EQ(MinSize<float>(), sizeof(float));
  EXPECT_EQ(MinSize<double>(), sizeof(double));
  EXPECT_EQ(MinSize<::zx::handle>(), sizeof(zx_handle_t));
  // TODO(markdittmer): Test handles to objects; e.g., ::zx::channel.
}

TEST(TraitsTest, EmptyMinSizesMatch) {
  EXPECT_EQ(MinSize<std::string>(), size0);
  EXPECT_EQ((MinSize<std::array<bool, 2>>()), size0);
  EXPECT_EQ(MinSize<std::vector<bool>>(), size0);
  EXPECT_EQ(MinSize<std::unique_ptr<bool>>(), size0);
}

TEST(TraitsTest, CantAllocateSizeTooSmall) {
  uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
  ::fuzzing::FuzzInput src(static_cast<uint8_t*>(data), 4);

  EXPECT_DEATH(
      {
        size_t size = sizeof(bool) - 1;
        Allocate<bool>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(uint8_t) - 1;
        Allocate<uint8_t>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(uint16_t) - 1;
        Allocate<uint16_t>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(uint32_t) - 1;
        Allocate<uint32_t>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(uint64_t) - 1;
        Allocate<uint64_t>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(int8_t) - 1;
        Allocate<int8_t>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(int16_t) - 1;
        Allocate<int16_t>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(int32_t) - 1;
        Allocate<int32_t>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(int64_t) - 1;
        Allocate<int64_t>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(float) - 1;
        Allocate<float>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(double) - 1;
        Allocate<double>{}(&src, &size);
      },
      "");
  EXPECT_DEATH(
      {
        size_t size = sizeof(zx_handle_t) - 1;
        Allocate<::zx::handle>{}(&src, &size);
      },
      "");
  // TODO(markdittmer): Test handles to objects; e.g., ::zx::channel.
}

TEST(TraitsTest, CanAllocateJustEnough) {
  EXPECT_EQ(DoAlloc<bool>().second, sizeof(bool));
  EXPECT_EQ(DoAlloc<uint8_t>().second, sizeof(uint8_t));
  EXPECT_EQ(DoAlloc<uint16_t>().second, sizeof(uint16_t));
  EXPECT_EQ(DoAlloc<uint32_t>().second, sizeof(uint32_t));
  EXPECT_EQ(DoAlloc<uint64_t>().second, sizeof(uint64_t));
  EXPECT_EQ(DoAlloc<int8_t>().second, sizeof(int8_t));
  EXPECT_EQ(DoAlloc<int16_t>().second, sizeof(int16_t));
  EXPECT_EQ(DoAlloc<int32_t>().second, sizeof(int32_t));
  EXPECT_EQ(DoAlloc<int64_t>().second, sizeof(int64_t));
  EXPECT_EQ(DoAlloc<float>().second, sizeof(float));
  EXPECT_EQ(DoAlloc<double>().second, sizeof(double));
  EXPECT_EQ(DoAlloc<::zx::handle>().second, sizeof(zx_handle_t));
  // TODO(markdittmer): Test handles to objects; e.g., ::zx::channel.
}

TEST(TestTraits, CanAllocateInnerMinSizeZero) {
  EXPECT_EQ((DoSizedAlloc<std::array<std::array<uint8_t, 2>, 0>>(0).second), size0);
  EXPECT_EQ((DoSizedAlloc<std::array<std::array<uint8_t, 0>, 2>>(0).second), size0);
  EXPECT_EQ((DoSizedAlloc<std::array<std::array<uint8_t, 0>, 0>>(0).second), size0);

  EXPECT_EQ((DoSizedAlloc<std::array<std::array<uint8_t, 2>, 0>>(sizeof(uint8_t) * 2).second),
            size0);
  EXPECT_EQ((DoSizedAlloc<std::array<std::array<uint8_t, 0>, 2>>(sizeof(uint8_t) * 2).second),
            size0);
  EXPECT_EQ((DoSizedAlloc<std::array<std::array<uint8_t, 0>, 0>>(sizeof(uint8_t) * 2).second),
            size0);

  std::pair<std::array<std::array<uint8_t, 2>, 2>, size_t> aa_and_size =
      DoSizedAlloc<std::array<std::array<uint8_t, 2>, 2>>(sizeof(uint8_t) * 4);
  EXPECT_EQ(aa_and_size.second, sizeof(uint8_t) * 4);
  ASSERT_EQ(aa_and_size.first.size(), size2);
  EXPECT_EQ(aa_and_size.first[0].size(), size2);
  EXPECT_EQ(aa_and_size.first[1].size(), size2);

  // Undersized allocation: Vector assumes MinSize<T>()=0 items take up at
  // at least 8 bytes each. Here, even though 4 bytes is enough for non-zero
  // vectors, the outer vector using the 8-byte heuristic assumes 4 bytes won't
  // be enough to allocate any items.
  {
    const size_t four_uint8_ts = sizeof(uint8_t) * 4;
    EXPECT_LT(four_uint8_ts, size8);
    std::pair<std::vector<std::vector<uint8_t>>, size_t> vv_and_size =
        DoSizedAlloc<std::vector<std::vector<uint8_t>>>(four_uint8_ts);
    EXPECT_EQ(vv_and_size.second, size0);
    EXPECT_EQ(vv_and_size.first.size(), size0);
  }

  // Single outer allocation: For heuristic reasons explained above, outer
  // vector will allocate 8 bytes for inner vector, yielding:
  // vector(vector(u8, u8, u8, u8, u8, u8, u8, u8)).
  {
    const size_t eight_uint8_ts = sizeof(uint8_t) * 8;
    EXPECT_EQ(eight_uint8_ts, size8);
    std::pair<std::vector<std::vector<uint8_t>>, size_t> vv_and_size =
        DoSizedAlloc<std::vector<std::vector<uint8_t>>>(eight_uint8_ts);
    EXPECT_EQ(vv_and_size.second, size8);
    ASSERT_EQ(vv_and_size.first.size(), size1);
    EXPECT_EQ(vv_and_size.first[0].size(), size8);
  }
}

}  // namespace
}  // namespace fuzzing
