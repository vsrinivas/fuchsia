// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/cancellable_helper.h"

#include <lib/fit/function.h>

#include "gtest/gtest.h"

namespace callback {
namespace {

TEST(CancellableImpl, CancelInvalidateCancellable) {
  bool is_cancelled = false;
  fxl::RefPtr<Cancellable> cancellable =
      CancellableImpl::Create([&is_cancelled] { is_cancelled = true; });

  EXPECT_FALSE(is_cancelled);
  EXPECT_FALSE(cancellable->IsDone());

  cancellable->Cancel();

  EXPECT_TRUE(is_cancelled);
  EXPECT_TRUE(cancellable->IsDone());
}

TEST(CancellableImpl, DoneInvalidateCancellable) {
  bool is_cancelled = false;
  fxl::RefPtr<CancellableImpl> cancellable =
      CancellableImpl::Create([&is_cancelled] { is_cancelled = true; });

  EXPECT_FALSE(is_cancelled);
  EXPECT_FALSE(cancellable->IsDone());

  cancellable->WrapCallback([] {})();

  EXPECT_FALSE(is_cancelled);
  EXPECT_TRUE(cancellable->IsDone());
}

TEST(CancellableImpl, WrappedCallbackNotCalledAfterCancel) {
  fxl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});

  bool called = false;
  auto wrapped_callback =
      cancellable->WrapCallback([&called] { called = true; });

  cancellable->Cancel();
  wrapped_callback();
  EXPECT_TRUE(cancellable->IsDone());
  EXPECT_FALSE(called);
}

TEST(CancellableImpl, DoneCallsOnDone) {
  fxl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});
  bool is_done = false;
  cancellable->SetOnDone([&is_done] { is_done = true; });

  EXPECT_FALSE(is_done);

  cancellable->WrapCallback([] {})();

  EXPECT_TRUE(is_done);
}

TEST(CancellableImpl, Wrap) {
  fxl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});

  bool called = false;
  cancellable->WrapCallback([&called] { called = true; })();
  EXPECT_TRUE(called);
}

TEST(CancellableImpl, DeleteWrappingCallbackInWrappedCallback) {
  fxl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});
  std::unique_ptr<fit::closure> callback;
  callback = std::make_unique<fit::closure>(
      cancellable->WrapCallback([&callback] { callback.reset(); }));
  cancellable = nullptr;
  (*callback)();
  EXPECT_EQ(nullptr, callback);
}

// Verifies that if the cancellable is cancelled within the wrapped callback,
// neither on_cancel_ nor on_done_ are called:
//  - we don't call on_done_ because Cancel() is called before the wrapped
//    callback completes
//  - we don't call on_cancel_ because the wrapped callback is executed (and not
//    cancelled)
TEST(CancellableImpl, CancelInWrappedCallback) {
  bool on_cancel_called = false;
  bool on_done_called = false;
  fxl::RefPtr<CancellableImpl> cancellable =
      CancellableImpl::Create([&on_cancel_called] { on_cancel_called = true; });
  cancellable->SetOnDone([&on_done_called] { on_done_called = true; });
  auto callback =
      cancellable->WrapCallback([cancellable] { cancellable->Cancel(); });
  cancellable = nullptr;
  callback();
  EXPECT_FALSE(on_cancel_called);
  EXPECT_FALSE(on_done_called);
}

}  //  namespace
}  //  namespace callback
