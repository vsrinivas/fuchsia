// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_WRITER_TESTS_H_
#define SRC_LIB_ZXDUMP_WRITER_TESTS_H_

#include <lib/zxdump/types.h>

#include <string_view>

#include <gtest/gtest.h>

namespace zxdump::testing {

using namespace std::literals;

inline ByteView AsBytes(std::string_view s) {
  return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

class WriterTest {
 public:
  static constexpr std::string_view kTestData = "foobarbazquuxchunk\0\0\0more"sv;
  static inline const auto kChunk = AsBytes("chunk"sv);
  static inline const auto kMore = AsBytes("more"sv);

  template <typename Writer>
  static void WriteTestData(Writer&& writer) {
    auto accum_fragment = writer.AccumulateFragmentsCallback();
    auto write_chunk = writer.WriteCallback();

    size_t offset = 0;
    constexpr size_t kExpectedOffset = 3 + 3 + 3 + 4;
    for (std::string_view s : {"foo"sv, "bar"sv, "baz"sv, "quux"sv}) {
      auto frag = AsBytes(s);
      auto result = accum_fragment(offset, frag);
      EXPECT_TRUE(result.is_ok());
      offset += frag.size();
    }
    EXPECT_EQ(offset, kExpectedOffset);

    auto frags_result = writer.WriteFragments();
    ASSERT_TRUE(frags_result.is_ok());
    EXPECT_EQ(frags_result.value(), offset);

    auto write_result = write_chunk(offset, kChunk);
    EXPECT_TRUE(write_result.is_ok());

    offset += kChunk.size() + 3;
    write_result = write_chunk(offset, kMore);
    EXPECT_TRUE(write_result.is_ok());
  }
};

}  // namespace zxdump::testing

#endif  // SRC_LIB_ZXDUMP_WRITER_TESTS_H_
