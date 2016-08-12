// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/mutex.h"
#include "gtest/gtest.h"

TEST(MagmaUtil, Mutex)
{
    magma::Mutex mutex;
    EXPECT_EQ(mutex.is_locked(), false);

    mutex.lock();
    EXPECT_EQ(mutex.is_locked(), true);

    mutex.unlock();
    EXPECT_EQ(mutex.is_locked(), false);

    bool ret = mutex.try_lock();
    EXPECT_EQ(ret, true);

    mutex.unlock();
    EXPECT_EQ(mutex.is_locked(), false);

    {
        magma::LockGuard lock_guard(mutex);
        EXPECT_EQ(mutex.is_locked(), true);
    }
    EXPECT_EQ(mutex.is_locked(), false);

    // TODO(MA-27) - add multithread mutex tests.
}
