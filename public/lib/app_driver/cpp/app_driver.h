// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_DRIVER_CPP_APP_DRIVER_H_
#define LIB_APP_DRIVER_CPP_APP_DRIVER_H_

#include <functional>
#include <memory>
#include <utility>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/lifecycle/cpp/lifecycle_impl.h"

namespace modular {

// AppDriver is a wrapper that simplifies participating in lifecycle management
// by the application's parent. It does this by exposing the Lifecycle service
// in app::ApplicationContext::outgoing_services() and proxies the Terminate()
// call of modular::Lifecycle to the Terminate() method on your application's
// class instance.
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
//   HelloWorldApp(app::ApplicationContext* app_context) {
//     app_context->outgoing_services()->AddService<..>(...);
//   }
//
//   void Terminate(const std::function<void()>& done) {
//     done();
//   }
// };
//
// int main(int argc, const char** argv) {
//   fsl::MessageLoop loop;
//   auto app_context = app::ApplicationContext::CreateFromStartupInfo();
//   modular::AppDriver<HelloWorldApp> driver(
//       app_context->outgoing_services(),
//       std::make_unique<HelloWorldApp>(app_context.get()),
//       [&loop] { loop.QuitNow(); });
//   loop.Run();
//   return 0;
// }
template <typename Impl>
class AppDriver : LifecycleImpl::Delegate {
 public:
  AppDriver(app::ServiceNamespace* const outgoing_services,
            std::unique_ptr<Impl> impl,
            std::function<void()> on_terminated)
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
      fsl::MessageLoop::GetCurrent()->task_runner()->PostTask([this] {
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
