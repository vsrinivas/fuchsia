// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>

// This header file has been generated from the strings library fuchsia.intl.l10n.
#include "fuchsia/intl/l10n/cpp/fidl.h"

namespace accessibility_test {
namespace {

// This test exists so that it can make sure that the string IDs resource is loaded together with
// the test binary package. It is not the goal of this test to test every single string auto
// generated ID.

using fuchsia::intl::l10n::MessageIds;

TEST(MessageIdTest, ResourcesAreLoadedWithBinary) {
  // It is enough to check for one message ID of the package we loaded.
  EXPECT_EQ(static_cast<uint64_t>(MessageIds::ROLE_HEADER),
            static_cast<uint64_t>(9495292885487086915u));
}

}  // namespace
}  // namespace accessibility_test
