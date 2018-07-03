// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_DRIVER_CPP_APP_DRIVER_H_
#define LIB_APP_DRIVER_CPP_APP_DRIVER_H_

#include <functional>
#include <memory>
#include <utility>

#include <lib/app/cpp/startup_context.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/memory/weak_ptr.h>
#include <lib/lifecycle/cpp/lifecycle_impl.h>

namespace modular {

// AppDriver is a wrapper that simplifies participating in lifecycle management
// by the application's parent. It does this by exposing the
// fuchsia::modular::Lifecycle service in
// fuchsia::sys::StartupContext::outgoing().deprecated_services() and proxies
// the Terminate() call of fuchsia::modular::Lifecycle to the Terminate() method
// on your application's class instance.
//
// Usage:
//
// NOTE: Your application's class must implement:
//
//     // Called by AppDriver. Call |done| once shutdown sequence is complete
//     // and |this| will be scheduled for deletion on the current MessageLoop.
//     void Terminate(const std::function<void()>& done);
//
// Example:
//
// class HelloWorldApp {
//  public:
//   HelloWorldApp(fuchsia::sys::StartupContext* context) {
//     context->outgoing().AddPublicService<..>(...);
//   }
//
//   void Terminate(const std::function<void()>& done) {
//     done();
//   }
// };
//
// int main(int argc, const char** argv) {
//   async::Loop loop(&kAsyncLoopConfigMakeDefault);
//   auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
//   modular::AppDriver<HelloWorldApp> driver(
//       context->outgoing().deprecated_services(),
//       std::make_unique<HelloWorldApp>(context.get()),
//       [&loop] { loop.Quit(); });
//   loop.Run();
//   return 0;
// }
template <typename Impl>
class AppDriver : LifecycleImpl::Delegate {
 public:
  AppDriver(fuchsia::sys::ServiceNamespace* const outgoing_services,
            std::unique_ptr<Impl> impl, std::function<void()> on_terminated)
      : lifecycle_impl_(outgoing_services, this),
        impl_(std::move(impl)),
        on_terminated_(std::move(on_terminated)) {}

 private:
  // |LifecycleImpl::Delegate|
  void Terminate() override {
    impl_->Terminate([this] {
      // Since this callback is called by |impl_|, we delay destroying it
      // until the current stack rolls back up to the MessageLoop which
      // guarantees impl_::Terminate() and anything that asynchronously
      // invokes this callback are done running.
      async::PostTask(async_get_default(), [this] {
        impl_.reset();
        on_terminated_();
      });
    });
  }

  LifecycleImpl lifecycle_impl_;
  std::unique_ptr<Impl> impl_;
  std::function<void()> on_terminated_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AppDriver);
};

}  // namespace modular

#endif  // LIB_APP_DRIVER_CPP_APP_DRIVER_H_
