// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/time.h>
#include <lib/async-testutils/async_stub.h>
#include <unittest/unittest.h>


namespace {

class FakeClockAsync : public async::AsyncStub {
public:
    zx::time Now() override { return current_time_; }
    void SetTime(zx::time t) { current_time_ = t; }

private:
    zx::time current_time_;
};

bool time_telling_test() {
    BEGIN_TEST;

    FakeClockAsync async;
    EXPECT_EQ(0u, async.Now().get());
    EXPECT_EQ(0u, async_now(&async));

    async.SetTime(zx::time(4u));
    EXPECT_EQ(4u, async.Now().get());
    EXPECT_EQ(4u, async_now(&async));

    async.SetTime(zx::time(1853u));
    EXPECT_EQ(1853u, async.Now().get());
    EXPECT_EQ(1853u, async_now(&async));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(time_tests)
RUN_TEST(time_telling_test)
END_TEST_CASE(time_tests)
