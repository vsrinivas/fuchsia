// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <unittest/unittest.h>

namespace {

// Counts instances.
class balance {
public:
    balance(int* counter)
        : counter_(counter) {
        *counter_ += 1;
    }

    balance(balance&& other)
        : counter_(other.counter_) {
        *counter_ += 1;
    }

    ~balance() {
        *counter_ -= 1;
    }

    balance(const balance& other) = delete;
    balance& operator=(const balance& other) = delete;
    balance& operator=(balance&& other) = delete;

private:
    int* const counter_;
};

void incr_arg(int* p) {
    *p += 1;
}

bool default_construction() {
    BEGIN_TEST;

    fit::deferred_action<fit::closure> d;
    EXPECT_FALSE(d);

    END_TEST;
}

bool basic() {
    BEGIN_TEST;

    int var = 0;
    {
        auto do_incr = fit::defer([&var]() { incr_arg(&var); });
        EXPECT_TRUE(do_incr);
        EXPECT_EQ(var, 0);
    }
    EXPECT_EQ(var, 1);

    END_TEST;
}

bool cancel() {
    BEGIN_TEST;

    int var = 0;
    {
        auto do_incr = fit::defer([&var]() { incr_arg(&var); });
        EXPECT_TRUE(do_incr);
        EXPECT_EQ(var, 0);

        do_incr.cancel();
        EXPECT_FALSE(do_incr);
        EXPECT_EQ(var, 0);

        // Once cancelled, call has no effect.
        do_incr.call();
        EXPECT_FALSE(do_incr);
        EXPECT_EQ(var, 0);
    }
    EXPECT_EQ(var, 0);

    END_TEST;
}

bool call() {
    BEGIN_TEST;

    int var = 0;
    {
        auto do_incr = fit::defer([&var]() { incr_arg(&var); });
        EXPECT_TRUE(do_incr);
        EXPECT_EQ(var, 0);

        do_incr.call();
        EXPECT_FALSE(do_incr);
        EXPECT_EQ(var, 1);

        // Call is effective only once.
        do_incr.call();
        EXPECT_FALSE(do_incr);
        EXPECT_EQ(var, 1);
    }
    EXPECT_EQ(var, 1);

    END_TEST;
}

bool recursive_call() {
    BEGIN_TEST;

    int var = 0;
    {
        auto do_incr = fit::defer<fit::closure>([]() { /* no-op */ });
        EXPECT_TRUE(do_incr);
        do_incr = fit::defer<fit::closure>([&do_incr, &var]() {
            incr_arg(&var);
            do_incr.call();
            EXPECT_FALSE(do_incr);
        });
        EXPECT_TRUE(do_incr);
        EXPECT_EQ(var, 0);

        do_incr.call();
        EXPECT_FALSE(do_incr);
        EXPECT_EQ(var, 1);
    }
    EXPECT_EQ(var, 1);

    END_TEST;
}

bool move_construct_basic() {
    BEGIN_TEST;

    int var = 0;
    {
        auto do_incr = fit::defer([&var]() { incr_arg(&var); });
        EXPECT_TRUE(do_incr);

        auto do_incr2(std::move(do_incr));
        EXPECT_FALSE(do_incr);
        EXPECT_TRUE(do_incr2);
        EXPECT_EQ(var, 0);
    }
    EXPECT_EQ(var, 1);

    END_TEST;
}

bool move_construct_from_canceled() {
    BEGIN_TEST;

    int var = 0;
    {
        auto do_incr = fit::defer([&var]() { incr_arg(&var); });
        EXPECT_TRUE(do_incr);

        do_incr.cancel();
        EXPECT_FALSE(do_incr);

        auto do_incr2(std::move(do_incr));
        EXPECT_FALSE(do_incr);
        EXPECT_FALSE(do_incr2);
        EXPECT_EQ(var, 0);
    }
    EXPECT_EQ(var, 0);

    END_TEST;
}

bool move_construct_from_called() {
    BEGIN_TEST;

    int var = 0;
    {
        auto do_incr = fit::defer([&var]() { incr_arg(&var); });
        EXPECT_TRUE(do_incr);
        EXPECT_EQ(var, 0);

        do_incr.call();
        EXPECT_FALSE(do_incr);
        EXPECT_EQ(var, 1);

        // Must not be called again, since do_incr has triggered already.
        auto do_incr2(std::move(do_incr));
        EXPECT_FALSE(do_incr);
    }
    EXPECT_EQ(var, 1);

    END_TEST;
}

bool move_assign_basic() {
    BEGIN_TEST;

    int var1 = 0, var2 = 0;
    {
        auto do_incr = fit::defer<fit::closure>([&var1]() { incr_arg(&var1); });
        auto do_incr2 = fit::defer<fit::closure>([&var2]() { incr_arg(&var2); });
        EXPECT_TRUE(do_incr);
        EXPECT_TRUE(do_incr2);
        EXPECT_EQ(var1, 0);
        EXPECT_EQ(var2, 0);

        // do_incr2 is moved-to, so its associated function is called.
        do_incr2 = std::move(do_incr);
        EXPECT_FALSE(do_incr);
        EXPECT_TRUE(do_incr2);
        EXPECT_EQ(var1, 0);
        EXPECT_EQ(var2, 1);
    }
    EXPECT_EQ(var1, 1);
    EXPECT_EQ(var2, 1);

    END_TEST;
}

bool move_assign_wider_scoped() {
    BEGIN_TEST;

    int var1 = 0, var2 = 0;
    {
        auto do_incr = fit::defer<fit::closure>([&var1]() { incr_arg(&var1); });
        EXPECT_TRUE(do_incr);
        EXPECT_EQ(var1, 0);
        EXPECT_EQ(var2, 0);
        {
            auto do_incr2 = fit::defer<fit::closure>([&var2]() { incr_arg(&var2); });
            EXPECT_TRUE(do_incr);
            EXPECT_TRUE(do_incr2);
            EXPECT_EQ(var1, 0);
            EXPECT_EQ(var2, 0);

            // do_incr is moved-to, so its associated function is called.
            do_incr = std::move(do_incr2);
            EXPECT_TRUE(do_incr);
            EXPECT_FALSE(do_incr2);
            EXPECT_EQ(var1, 1);
            EXPECT_EQ(var2, 0);
        }
        // do_incr2 is out of scope but has been moved so its function is not
        // called.
        EXPECT_TRUE(do_incr);
        EXPECT_EQ(var1, 1);
        EXPECT_EQ(var2, 0);
    }
    EXPECT_EQ(var1, 1);
    EXPECT_EQ(var2, 1);

    END_TEST;
}

bool move_assign_from_canceled() {
    BEGIN_TEST;

    int var1 = 0, var2 = 0;
    {
        auto do_incr = fit::defer<fit::closure>([&var1]() { incr_arg(&var1); });
        auto do_incr2 = fit::defer<fit::closure>([&var2]() { incr_arg(&var2); });
        EXPECT_TRUE(do_incr);
        EXPECT_TRUE(do_incr2);
        EXPECT_EQ(var1, 0);
        EXPECT_EQ(var2, 0);

        do_incr.cancel();
        EXPECT_FALSE(do_incr);
        EXPECT_TRUE(do_incr2);
        EXPECT_EQ(var1, 0);
        EXPECT_EQ(var2, 0);

        // do_incr2 is moved-to, so its associated function is called.
        do_incr2 = std::move(do_incr);
        EXPECT_FALSE(do_incr);
        EXPECT_FALSE(do_incr2);
        EXPECT_EQ(var1, 0);
        EXPECT_EQ(var2, 1);
    }
    // do_incr was cancelled, this state is preserved by the move.
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 1);

    END_TEST;
}

bool move_assign_from_called() {
    BEGIN_TEST;

    int var1 = 0, var2 = 0;
    {
        auto do_incr = fit::defer<fit::closure>([&var1]() { incr_arg(&var1); });
        auto do_incr2 = fit::defer<fit::closure>([&var2]() { incr_arg(&var2); });
        EXPECT_TRUE(do_incr);
        EXPECT_TRUE(do_incr2);
        EXPECT_EQ(var1, 0);
        EXPECT_EQ(var2, 0);

        do_incr.call();
        EXPECT_FALSE(do_incr);
        EXPECT_TRUE(do_incr2);
        EXPECT_EQ(var1, 1);
        EXPECT_EQ(var2, 0);

        // do_incr2 is moved-to, so its associated function is called.
        do_incr2 = std::move(do_incr);
        EXPECT_FALSE(do_incr);
        EXPECT_FALSE(do_incr2);
        EXPECT_EQ(var1, 1);
        EXPECT_EQ(var2, 1);
    }
    // do_incr was called already, this state is preserved by the move.
    EXPECT_EQ(var1, 1);
    EXPECT_EQ(var2, 1);

    END_TEST;
}

bool target_destroyed_when_scope_exited() {
    BEGIN_TEST;

    int call_count = 0;
    int instance_count = 0;
    {
        auto action = fit::defer(
            [&call_count, balance = balance(&instance_count) ] {
                incr_arg(&call_count);
            });
        EXPECT_EQ(0, call_count);
        EXPECT_EQ(1, instance_count);
    }
    EXPECT_EQ(1, call_count);
    EXPECT_EQ(0, instance_count);

    END_TEST;
}

bool target_destroyed_when_called() {
    BEGIN_TEST;

    int call_count = 0;
    int instance_count = 0;
    {
        auto action = fit::defer(
            [&call_count, balance = balance(&instance_count) ] {
                incr_arg(&call_count);
            });
        EXPECT_EQ(0, call_count);
        EXPECT_EQ(1, instance_count);

        action.call();
        EXPECT_EQ(1, call_count);
        EXPECT_EQ(0, instance_count);
    }
    EXPECT_EQ(1, call_count);
    EXPECT_EQ(0, instance_count);

    END_TEST;
}

bool target_destroyed_when_canceled() {
    BEGIN_TEST;

    int call_count = 0;
    int instance_count = 0;
    {
        auto action = fit::defer(
            [&call_count, balance = balance(&instance_count) ] {
                incr_arg(&call_count);
            });
        EXPECT_EQ(0, call_count);
        EXPECT_EQ(1, instance_count);

        action.cancel();
        EXPECT_EQ(0, call_count);
        EXPECT_EQ(0, instance_count);
    }
    EXPECT_EQ(0, call_count);
    EXPECT_EQ(0, instance_count);

    END_TEST;
}

bool target_destroyed_when_move_constructed() {
    BEGIN_TEST;

    int call_count = 0;
    int instance_count = 0;
    {
        auto action = fit::defer(
            [&call_count, balance = balance(&instance_count) ] {
                incr_arg(&call_count);
            });
        EXPECT_EQ(0, call_count);
        EXPECT_EQ(1, instance_count);

        auto action2(std::move(action));
        EXPECT_EQ(0, call_count);
        EXPECT_EQ(1, instance_count);
    }
    EXPECT_EQ(1, call_count);
    EXPECT_EQ(0, instance_count);

    END_TEST;
}

bool target_destroyed_when_move_assigned() {
    BEGIN_TEST;

    int call_count = 0;
    int instance_count = 0;
    {
        auto action = fit::defer<fit::closure>(
            [&call_count, balance = balance(&instance_count) ] {
                incr_arg(&call_count);
            });
        EXPECT_EQ(0, call_count);
        EXPECT_EQ(1, instance_count);

        auto action2 = fit::defer<fit::closure>([] {});
        action2 = std::move(action);
        EXPECT_EQ(0, call_count);
        EXPECT_EQ(1, instance_count);
    }
    EXPECT_EQ(1, call_count);
    EXPECT_EQ(0, instance_count);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(defer_tests)
RUN_TEST(default_construction)
RUN_TEST(basic)
RUN_TEST(cancel)
RUN_TEST(call)
RUN_TEST(recursive_call)
RUN_TEST(move_construct_basic)
RUN_TEST(move_construct_from_canceled)
RUN_TEST(move_construct_from_called)
RUN_TEST(move_assign_basic)
RUN_TEST(move_assign_wider_scoped)
RUN_TEST(move_assign_from_canceled)
RUN_TEST(move_assign_from_called)
RUN_TEST(target_destroyed_when_scope_exited)
RUN_TEST(target_destroyed_when_called)
RUN_TEST(target_destroyed_when_canceled)
RUN_TEST(target_destroyed_when_move_constructed)
RUN_TEST(target_destroyed_when_move_assigned)
END_TEST_CASE(defer_tests)
