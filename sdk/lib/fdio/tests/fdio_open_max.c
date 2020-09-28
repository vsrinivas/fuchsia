// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/fdio/limits.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

TEST(OpenMaxText, Sysconf) { ASSERT_EQ(sysconf(_SC_OPEN_MAX), FDIO_MAX_FD); }
