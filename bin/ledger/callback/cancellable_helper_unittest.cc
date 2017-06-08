// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/cancellable_helper.h"
#include "gtest/gtest.h"

namespace callback {
namespace {

TEST(CancellableImpl, CancelInvalidateCancellable) {
  bool is_cancelled = false;
  ftl::RefPtr<Cancellable> cancellable =
      CancellableImpl::Create([&is_cancelled] { is_cancelled = true; });

  EXPECT_FALSE(is_cancelled);
  EXPECT_FALSE(cancellable->IsDone());

  cancellable->Cancel();

  EXPECT_TRUE(is_cancelled);
  EXPECT_TRUE(cancellable->IsDone());
}

TEST(CancellableImpl, DoneInvalidateCancellable) {
  bool is_cancelled = false;
  ftl::RefPtr<CancellableImpl> cancellable =
      CancellableImpl::Create([&is_cancelled] { is_cancelled = true; });

  EXPECT_FALSE(is_cancelled);
  EXPECT_FALSE(cancellable->IsDone());

  cancellable->WrapCallback([] {})();

  EXPECT_FALSE(is_cancelled);
  EXPECT_TRUE(cancellable->IsDone());
}

TEST(CancellableImpl, WrappedCallbackNotCalledAfterCancel) {
  ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});

  bool called = false;
  auto wrapped_callback =
      cancellable->WrapCallback([&called] { called = true; });

  cancellable->Cancel();
  wrapped_callback();
  EXPECT_TRUE(cancellable->IsDone());
  EXPECT_FALSE(called);
}

TEST(CancellableImpl, DoneCallsOnDone) {
  ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});
  bool is_done = false;
  cancellable->SetOnDone([&is_done] { is_done = true; });

  EXPECT_FALSE(is_done);

  cancellable->WrapCallback([] {})();

  EXPECT_TRUE(is_done);
}

TEST(CancellableImpl, Wrap) {
  ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});

  bool called = false;
  cancellable->WrapCallback([&called] { called = true; })();
  EXPECT_TRUE(called);
}

TEST(CancellableImpl, DeleteWrappingCallbackInWrappedCallback) {
  ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});
  std::unique_ptr<ftl::Closure> callback;
  callback = std::make_unique<ftl::Closure>(
      cancellable->WrapCallback([&callback] { callback.reset(); }));
  cancellable = nullptr;
  (*callback)();
  EXPECT_EQ(nullptr, callback);
}

TEST(CancellableImpl, CancelInWrappedCallback) {
  bool on_cancel_called = false;
  ftl::RefPtr<CancellableImpl> cancellable =
      CancellableImpl::Create([&on_cancel_called] { on_cancel_called = true; });
  auto callback =
      cancellable->WrapCallback([cancellable] { cancellable->Cancel(); });
  cancellable = nullptr;
  callback();
  EXPECT_FALSE(on_cancel_called);
}

}  //  namespace
}  //  namespace callback
