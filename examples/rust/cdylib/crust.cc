// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/rust/cdylib/rust/crust.h"
#include <gtest/gtest.h>

TEST(DyLibTest, GetInt) { EXPECT_EQ(42, crust_get_int()); }
