// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests Transaction Limits behavior.

#include "src/storage/minfs/transaction_limits.h"

#include <zircon/assert.h>
#include <zircon/errors.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/storage/minfs/format.h"

namespace minfs {
namespace {

TEST(GetRequiredBlockCountTest, InvalidBlockSize) {
  ASSERT_EQ(GetRequiredBlockCount(0, 0, kMinfsBlockSize - 1).error_value(), ZX_ERR_INVALID_ARGS);
}

TEST(GetRequiredBlockCountTest, ZeroLength) {
  ASSERT_EQ(GetRequiredBlockCount(0, 0, kMinfsBlockSize).value(), 0u);
}

}  // namespace
}  // namespace minfs
