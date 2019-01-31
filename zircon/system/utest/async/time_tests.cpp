// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/time.h>
#include <lib/async-testutils/dispatcher_stub.h>
#include <unittest/unittest.h>


namespace {

class FakeClockAsync : public async::DispatcherStub {
public:
    zx::time Now() override { return current_time_; }
    void SetTime(zx::time t) { current_time_ = t; }

private:
    zx::time current_time_;
};

bool time_telling_test() {
    BEGIN_TEST;

    FakeClockAsync dispatcher;
    EXPECT_EQ(0u, dispatcher.Now().get());
    EXPECT_EQ(0u, async_now(&dispatcher));

    dispatcher.SetTime(zx::time(4u));
    EXPECT_EQ(4u, dispatcher.Now().get());
    EXPECT_EQ(4u, async_now(&dispatcher));

    dispatcher.SetTime(zx::time(1853u));
    EXPECT_EQ(1853u, dispatcher.Now().get());
    EXPECT_EQ(1853u, async_now(&dispatcher));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(time_tests)
RUN_TEST(time_telling_test)
END_TEST_CASE(time_tests)
