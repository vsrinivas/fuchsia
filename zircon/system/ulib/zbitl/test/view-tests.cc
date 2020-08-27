// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests.h"

namespace {

struct TestEmptyTupleTraits {
  using storage_type = std::tuple<>;
};

template <typename T>
struct BasicStringIo {
  using storage_type = std::basic_string_view<T>;

  void Create(fbl::unique_fd fd, size_t size, std::basic_string_view<T>* zbi) {
    ASSERT_TRUE(fd);
    const size_t n = (size + sizeof(T) - 1) / sizeof(T);
    ASSERT_LE(n, sizeof(buff_));
    ASSERT_EQ(size, read(fd.get(), buff_, size), "%s", strerror(errno));
    *zbi = std::basic_string_view<T>(reinterpret_cast<const T*>(buff_), n / sizeof(T));
  }

  void ReadPayload(std::basic_string_view<T> zbi, const zbi_header_t& header,
                   std::basic_string_view<T> payload, std::string* string) {
    *string =
        std::string(reinterpret_cast<const char*>(payload.data()), payload.size() * sizeof(T));
  }

  char buff_[kMaxZbiSize];
};

using StringIo = BasicStringIo<char>;
using ByteViewIo = BasicStringIo<std::byte>;

TEST(ZbitlViewEmptyTupleTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<TestEmptyTupleTraits>(true));
}

TEST(ZbitlViewStringTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<StringIo>(false));
}

TEST(ZbitlViewStringTests, EmptyZbi) { ASSERT_NO_FATAL_FAILURES(TestEmptyZbi<StringIo>()); }

TEST(ZbitlViewStringTests, SimpleZbi) { ASSERT_NO_FATAL_FAILURES(TestSimpleZbi<StringIo>()); }

TEST(ZbitlViewStringTests, BadCrcZbi) { ASSERT_NO_FATAL_FAILURES(TestBadCrcZbi<StringIo>()); }

TEST(ZbitlViewByteViewTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<ByteViewIo>(false));
}

TEST(ZbitlViewByteViewTests, EmptyZbi) { ASSERT_NO_FATAL_FAILURES(TestEmptyZbi<ByteViewIo>()); }

TEST(ZbitlViewByteViewTests, SimpleZbi) { ASSERT_NO_FATAL_FAILURES(TestSimpleZbi<ByteViewIo>()); }

TEST(ZbitlViewByteViewTests, BadCrcZbi) { ASSERT_NO_FATAL_FAILURES(TestBadCrcZbi<ByteViewIo>()); }

}  // namespace
