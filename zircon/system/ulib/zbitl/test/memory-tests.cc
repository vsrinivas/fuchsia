// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory-tests.h"

namespace {

using FblByteSpanTestTraits = FblSpanTestTraits<std::byte>;
using FblUint64ArrayTestTraits = FblArrayTestTraits<uint64_t>;

TEST(ZbitlViewFblByteSpanTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<FblByteSpanTestTraits>());
}

TEST(ZbitlViewFblByteSpanTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<FblByteSpanTestTraits>());
}

TEST_ITERATION(ZbitlViewFblByteSpanTests, FblByteSpanTestTraits)

TEST_MUTATION(ZbitlViewFblByteSpanTests, FblByteSpanTestTraits)

TEST(ZbitlViewFblByteArrayTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<FblByteArrayTestTraits>());
}

TEST(ZbitlViewFblByteArrayTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<FblByteArrayTestTraits>());
}

TEST_ITERATION(ZbitlViewFblByteArrayTests, FblByteArrayTestTraits)

TEST_MUTATION(ZbitlViewFblByteArrayTests, FblByteArrayTestTraits)

TEST_COPY_CREATION(ZbitlViewFblByteArrayTests, FblByteArrayTestTraits)

TEST(ZbitlImageFblByteArrayTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<FblByteArrayTestTraits>());
}

TEST(ZbitlViewFblUint64ArrayTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<FblUint64ArrayTestTraits>());
}

// TODO(joshuaseaton): Use ZBIs with payload size divisible by eight so we can
// further test FblUint64ArrayTestTraits.

}  // namespace
