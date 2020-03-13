// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_COMMON_ASYNC_HOLDER_H_
#define SRC_MODULAR_LIB_COMMON_ASYNC_HOLDER_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <functional>
#include <memory>
#include <string>

#include "src/lib/fxl/macros.h"
#include "src/lib/syslog/cpp/logger.h"

namespace modular {

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
  void Teardown(zx::duration timeout, fit::function<void()> done);

 private:
  // Called by Teardown(). A timeout callback is scheduled simultaneously.
  // Eventually ImplReset() is called, either when done() is invoked, or when
  // the timeout elapses.
  virtual void ImplTeardown(fit::function<void()> done) = 0;

  // Called after either the done callback of ImplTeardown() is invoked, or the
  // timeout elapses. The timeout is the reason ImplReset() is separate from
  // ImplTeardown().
  virtual void ImplReset() = 0;

  // For log messages only.
  const std::string name_;

  // This is the flag shared with the done and timeout callbacks of Teardown()
  // that prevents double invocation. The destructor sets it to true to prevent
  // pending callbacks from executing if the instance is deleted while a
  // teardown is pending. This may happen when the Teardown() of the instance
  // this holder is a member of runs into a timeout on its own.
  std::shared_ptr<bool> down_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AsyncHolderBase);
};

template <class Impl>
class AsyncHolder : public AsyncHolderBase {
 public:
  AsyncHolder(const char* const name) : AsyncHolderBase(name) {}
  ~AsyncHolder() override = default;

  void reset(Impl* const impl) { impl_.reset(impl); }

  // Must not be used to invoke Impl::Teardown().
  Impl* operator->() {
    FX_DCHECK(impl_.get());
    return impl_.get();
  }

  // Must not be used to invoke Impl::Teardown().
  Impl* get() { return impl_.get(); }

 private:
  void ImplTeardown(fit::function<void()> done) override {
    FX_DCHECK(impl_.get());
    impl_->Teardown(std::move(done));
  }

  void ImplReset() override { impl_.reset(); }

  std::unique_ptr<Impl> impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AsyncHolder);
};

// ClosureAsyncHolder is a lightweight AsyncHolder that lets the client provide
// the teardown and reset implementation as callbacks.
class ClosureAsyncHolder : public AsyncHolderBase {
 public:
  using DoneCallback = fit::function<void()>;

  ClosureAsyncHolder(std::string name, fit::function<void(DoneCallback)> on_teardown);
  ClosureAsyncHolder(std::string name, fit::function<void(DoneCallback)> on_teardown,
                     fit::function<void()> on_reset);
  ~ClosureAsyncHolder() override;

 private:
  fit::function<void(DoneCallback)> on_teardown_;
  fit::function<void()> on_reset_;

  // Implementation of |AsyncHolderBase|
  void ImplTeardown(fit::function<void()> done) override;
  void ImplReset() override;

  FXL_DISALLOW_COPY_AND_ASSIGN(ClosureAsyncHolder);
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_COMMON_ASYNC_HOLDER_H_
