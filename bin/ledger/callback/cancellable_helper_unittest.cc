// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/cancellable_helper.h"
#include "gtest/gtest.h"

namespace callback {
namespace {

TEST(CancellableImpl, CancelInvalidateCancellable) {
  bool is_cancelled;
  ftl::RefPtr<Cancellable> cancellable =
      CancellableImpl::Create([&is_cancelled] { is_cancelled = true; });

  EXPECT_FALSE(is_cancelled);
  EXPECT_FALSE(cancellable->IsDone());

  cancellable->Cancel();

  EXPECT_TRUE(is_cancelled);
  EXPECT_TRUE(cancellable->IsDone());
}

TEST(CancellableImpl, DoneInvalidateCancellable) {
  bool is_cancelled;
  ftl::RefPtr<CancellableImpl> cancellable =
      CancellableImpl::Create([&is_cancelled] { is_cancelled = true; });

  EXPECT_FALSE(is_cancelled);
  EXPECT_FALSE(cancellable->IsDone());

  cancellable->WrapCallback([] {})();

  EXPECT_FALSE(is_cancelled);
  EXPECT_TRUE(cancellable->IsDone());
}

TEST(CancellableImpl, DoneCallsOnDone) {
  ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});
  bool is_done;
  cancellable->OnDone([&is_done] { is_done = true; });

  EXPECT_FALSE(is_done);

  cancellable->WrapCallback([] {})();

  EXPECT_TRUE(is_done);
}

TEST(CancellableImpl, WrapWithVoidValue) {
  ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});

  bool called = false;
  cancellable->WrapCallback([&called] { called = true; })();
  EXPECT_TRUE(called);
}

TEST(CancellableImpl, WrapWithReturnValue) {
  ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});

  bool called = false;
  EXPECT_EQ(1, cancellable->WrapCallback([&called] {
    called = true;
    return 1;
  })());
  EXPECT_TRUE(called);
}

TEST(CancellableImpl, DeleteWrappingCallbackInWrappedCallback) {
  {
    ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});
    std::unique_ptr<std::function<void()>> callback;
    callback = std::make_unique<std::function<void()>>(
        cancellable->WrapCallback([&callback] { callback.reset(); }));
    cancellable = nullptr;
    (*callback)();
    EXPECT_EQ(nullptr, callback);
  }

  {
    ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});
    std::unique_ptr<std::function<int()>> callback;
    callback = std::make_unique<std::function<int()>>(
        cancellable->WrapCallback([&callback] {
          callback.reset();
          return 0;
        }));
    cancellable = nullptr;
    (*callback)();
    EXPECT_EQ(nullptr, callback);
  }
}

TEST(CancellableImpl, CancelInWrappedCallback) {
  {
    ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});
    auto callback =
        cancellable->WrapCallback([cancellable] { cancellable->Cancel(); });
    cancellable = nullptr;
    callback();
  }

  {
    ftl::RefPtr<CancellableImpl> cancellable = CancellableImpl::Create([] {});
    auto callback = cancellable->WrapCallback([cancellable] {
      cancellable->Cancel();
      return 0;
    });
    cancellable = nullptr;
    callback();
  }
}

}  //  namespace
}  //  namespace callback
