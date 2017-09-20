// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_COMMON_ASYNC_HOLDER_
#define APPS_MODULAR_LIB_COMMON_ASYNC_HOLDER_

#include <functional>
#include <memory>
#include <string>

#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace modular {

constexpr int kAsyncHolderTimeoutSeconds = 1;

// A smart pointer that holds on to an implementation class and is able to
// invoke Teardown() on the implementation with a timeout.
//
// TODO(mesch): The name is a bit of a mouthful. It's very similar to AppClient
// and should align with that, but it owns impl and isn't a client of it.
class AsyncHolderBase {
 public:
  AsyncHolderBase(std::string name);
  virtual ~AsyncHolderBase();

  // Timeout is the first argument because: (1) the second argument can be very
  // long, and it's incongruent to have the timeout dangling after it, (2) the
  // timeout happens first, the done callback after that, so this ordering is
  // actually quite natural.
  void Teardown(fxl::TimeDelta timeout, std::function<void()> done);

 private:
  // Called by Teardown(). A timeout callback is scheduled simultaneously.
  // Eventually ImplReset() is called, either when done() is invoked, or when
  // the timeout elapses.
  virtual void ImplTeardown(std::function<void()> done) = 0;

  // Called after either the done callback of ImplTeardown() is invoked, or the
  // timeout elapses. The timeout is the reason ImplReset() is separate from
  // ImplTeardown().
  virtual void ImplReset() = 0;

  const std::string name_;  // For log messages only.
  FXL_DISALLOW_COPY_AND_ASSIGN(AsyncHolderBase);
};

template<class Impl>
class AsyncHolder : public AsyncHolderBase {
 public:
  AsyncHolder(const char* const name) : AsyncHolderBase(name) {}
  ~AsyncHolder() override = default;

  void reset(Impl* const impl) {
    impl_.reset(impl);
  }

  // Must not be used to invoke Impl::Teardown().
  Impl* operator->() {
    FXL_DCHECK(impl_.get());
    return impl_.get();
  }

  // Must not be used to invoke Impl::Teardown().
  Impl* get() {
    return impl_.get();
  }

 private:
  void ImplTeardown(std::function<void()> done) override {
    FXL_DCHECK(impl_.get());
    impl_->Teardown(done);
  }

  void ImplReset() override {
    impl_.reset();
  }

  std::unique_ptr<Impl> impl_;
  FXL_DISALLOW_COPY_AND_ASSIGN(AsyncHolder);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_COMMON_ASYNC_HOLDER_
