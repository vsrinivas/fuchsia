// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mman.h>

#include <zxtest/zxtest.h>

// TODO(60236): get the memfd_create symbol from libc instead.
#include "memfd.h"

TEST(MemFDTest, Create) { EXPECT_GT(memfd_create(0, 0), 0); }
