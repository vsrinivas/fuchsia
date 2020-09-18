// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vmo-tests.h"

#include "tests.h"

namespace {

TEST(ZbitlViewVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<VmoTestTraits>());
}

TEST(ZbitlViewVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<VmoTestTraits>());
}

TEST_ITERATION(ZbitlViewVmoTests, VmoTestTraits)

TEST_MUTATION(ZbitlViewVmoTests, VmoTestTraits)

TEST_COPY_CREATION(ZbitlViewVmoTests, VmoTestTraits)

TEST(ZbitlViewUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<UnownedVmoTestTraits>());
}

TEST(ZbitlViewUnownedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<UnownedVmoTestTraits>());
}

TEST_ITERATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST_MUTATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST(ZbitlViewMapUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<MapUnownedVmoTestTraits>());
}

TEST(ZbitlViewMapUnownedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<MapUnownedVmoTestTraits>());
}

// Note that the iterations over many-small-items.zbi and
// second-item-on-page-boundary.zbi with CRC32 checking will cover the cases of
// mapping window re-use and replacement, respectively.
TEST_ITERATION(ZbitlViewMapUnownedVmoTests, MapUnownedVmoTestTraits)

TEST_MUTATION(ZbitlViewMapUnownedVmoTests, MapUnownedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewMapUnownedVmoTests, MapUnownedVmoTestTraits)

TEST(ZbitlViewMapOwnedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<MapOwnedVmoTestTraits>());
}

TEST(ZbitlViewMapOwnedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<MapOwnedVmoTestTraits>());
}

TEST_ITERATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST_MUTATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

}  // namespace
