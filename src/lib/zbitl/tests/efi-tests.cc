// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "efi-tests.h"

#include "bootfs-tests.h"
#include "tests.h"

namespace {

// The type of efi_file_protocol* cannot be default-constructed, so we skip the
// TestDefaultConstructedView() test case.

TEST_ITERATION(ZbitlViewEfiTests, EfiTestTraits)

TEST_MUTATION(ZbitlViewEfiTests, EfiTestTraits)

TEST(ZbitlViewEfiTests, CreateFromBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestViewFromBogusZbi<EfiTestTraits>());
}

TEST(ZbitlImageEfiTests, Appending) { ASSERT_NO_FATAL_FAILURE(TestAppending<EfiTestTraits>()); }

TEST(ZbitlBootfsEfiTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<EfiTestTraits>());
}

}  // namespace
