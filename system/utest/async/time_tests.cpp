// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/time.h>
#include <unittest/unittest.h>

#include "async_stub.h"

namespace {

class FakeClockAsync : public AsyncStub {
public:
    zx_time_t Now() override { return current_time_; }
    void SetTime(zx_time_t t) { current_time_ = t; }

private:
    zx_time_t current_time_ = 0u;
};

bool time_telling_test() {
    BEGIN_TEST;

    FakeClockAsync async;
    EXPECT_EQ(0u, async.Now());
    EXPECT_EQ(0u, async_now(&async));

    async.SetTime(4u);
    EXPECT_EQ(4u, async.Now());
    EXPECT_EQ(4u, async_now(&async));

    async.SetTime(1853u);
    EXPECT_EQ(1853u, async.Now());
    EXPECT_EQ(1853u, async_now(&async));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(time_tests)
RUN_TEST(time_telling_test)
END_TEST_CASE(time_tests)
