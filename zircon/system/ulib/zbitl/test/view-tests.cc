// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "view-tests.h"

#include <tuple>

namespace {

struct EmptyTupleTestTraits {
  using storage_type = std::tuple<>;

  static constexpr bool kDefaultConstructedViewHasStorageError = true;
};

// The DefaultConstructed case is the only one that std::tuple<> passes since
// every other case requires readable storage.
TEST(ZbitlViewEmptyTupleTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<EmptyTupleTestTraits>());
}

TEST(ZbitlViewStringTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<StringTestTraits>());
}

TEST(ZbitlViewStringTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<StringTestTraits>());
}

TEST_ITERATION(ZbitlViewStringTests, StringTestTraits)

TEST(ZbitlViewByteViewTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<ByteViewTestTraits>());
}

TEST(ZbitlViewByteViewTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<ByteViewTestTraits>());
}

TEST_ITERATION(ZbitlViewByteViewTests, ByteViewTestTraits)

}  // namespace
