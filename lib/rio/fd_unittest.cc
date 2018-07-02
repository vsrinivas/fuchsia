// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/rio/fd.h"

#include <fcntl.h>

#include <lib/fxl/files/unique_fd.h>

#include "gtest/gtest.h"

namespace rio {
namespace {

TEST(RioFdTest, CloneChannel) {
  fxl::UniqueFD root_fd(open("/svc", O_PATH));
  ASSERT_TRUE(root_fd.is_valid());
  EXPECT_TRUE(CloneChannel(root_fd.get()).is_valid());
}

}  // namespace
}  // namespace rio
