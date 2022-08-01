// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/internal/transport.h>

#include <zxtest/zxtest.h>

TEST(Transport, AnyTransportWaiter) {
  using CancellationResult = fidl::internal::TransportWaiter::CancellationResult;

  struct TestTransportWaiter : fidl::internal::TransportWaiter {
    TestTransportWaiter(zx_status_t begin_result, CancellationResult cancel_result,
                        size_t* destruct_count)
        : begin_result(begin_result),
          cancel_result(cancel_result),
          destruct_count(destruct_count) {}
    ~TestTransportWaiter() override { (*destruct_count)++; }

    zx_status_t begin_result;
    CancellationResult cancel_result;
    size_t* destruct_count;

    zx_status_t Begin() override { return begin_result; }
    CancellationResult Cancel() override { return cancel_result; }
  };

  size_t destruct_count_a = 0;
  size_t destruct_count_b = 0;
  {
    fidl::internal::AnyTransportWaiter any_waiter;

    TestTransportWaiter& waiter_a =
        any_waiter.emplace<TestTransportWaiter>(1, CancellationResult::kOk, &destruct_count_a);
    ASSERT_EQ(0, destruct_count_a);
    ASSERT_EQ(1, waiter_a.begin_result);
    ASSERT_EQ(CancellationResult::kOk, waiter_a.cancel_result);
    ASSERT_EQ(1, any_waiter->Begin());
    ASSERT_EQ(CancellationResult::kOk, any_waiter->Cancel());
    TestTransportWaiter& waiter_b = any_waiter.emplace<TestTransportWaiter>(
        3, CancellationResult::kNotFound, &destruct_count_b);
    ASSERT_EQ(1, destruct_count_a);
    ASSERT_EQ(0, destruct_count_b);

    ASSERT_EQ(3, waiter_b.begin_result);
    ASSERT_EQ(CancellationResult::kNotFound, waiter_b.cancel_result);
    ASSERT_EQ(3, any_waiter->Begin());
    ASSERT_EQ(CancellationResult::kNotFound, any_waiter->Cancel());
    ASSERT_EQ(0, destruct_count_b);
  }

  ASSERT_EQ(1, destruct_count_b);
}
