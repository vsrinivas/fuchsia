// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_PUBLIC_LIB_APP_DRIVER_CPP_APP_DRIVER_H_
#define PERIDOT_PUBLIC_LIB_APP_DRIVER_CPP_APP_DRIVER_H_

#include <functional>
#include <memory>
#include <utility>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/public/lib/lifecycle/cpp/lifecycle_impl.h"

namespace modular {

// AppDriver provides a way to participate an application that participates in
// the application lifecycle.
//
// class HelloWorldApp {
//  public:
//   HelloWorldApp(app::ApplicationContext* app_context) {
//     app_context->outgoing_services()->AddService<..>(...);
//   }
//
//   // Called by AppDriver.
//   void Terminate(const std::function<void()>& done) {
//     done();
//     // |this| is deleted.
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
  AppDriver(app::ServiceNamespace* outgoing_services,
            std::unique_ptr<Impl> impl,
            std::function<void()> on_terminated)
      : lifecycle_impl_(outgoing_services, this),
        impl_(std::move(impl)),
        on_terminated_(on_terminated) {}

 private:
  // |LifecycleImpl::Delegate|
  void Terminate() override {
    impl_->Terminate([this] {
      impl_.reset();
      on_terminated_();
    });
  }

  LifecycleImpl lifecycle_impl_;
  std::unique_ptr<Impl> impl_;
  std::function<void()> on_terminated_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AppDriver);
};

}  // namespace modular

#endif  // PERIDOT_PUBLIC_LIB_APP_DRIVER_CPP_APP_DRIVER_H_
