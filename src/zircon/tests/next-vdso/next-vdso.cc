// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls-next.h>

#include <zxtest/zxtest.h>

TEST(NextVdsoTest, UserTest) { ASSERT_OK(zx_syscall_next_1(7)); }
