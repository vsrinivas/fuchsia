// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_DRIVER_CPP_MODULE_DRIVER_H_
#define LIB_APP_DRIVER_CPP_MODULE_DRIVER_H_

#include <memory>

#include <component/cpp/fidl.h>
#include <modular/cpp/fidl.h>
#include <views_v1/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/lifecycle/cpp/lifecycle_impl.h"

namespace modular {

// This interface is passed to the |Impl| object that ModuleDriver initializes.
class ModuleHost {
 public:
  virtual component::ApplicationContext* application_context() = 0;
  virtual ModuleContext* module_context() = 0;
};

// ModuleDriver provides a way to write modules and participate in application
// lifecycle. The |Impl| class supplied to ModuleDriver is instantiated when the
// Module and ViewProvider services have both been requested by the framework.
//
// Usage:
//   The |Impl| class must implement:
//
//      // A constructor with the following signature:
//      Constructor(
//           modular::ModuleHost* module_host,
//           fidl::InterfaceRequest<views_v1::ViewProvider> view_provider_request);
//
//       // Called by ModuleDriver. Call |done| once shutdown sequence is
//       // complete, at which point |this| will be deleted.
//       void Terminate(const std::function<void()>& done);
//
// Example:
//
// class HelloWorldModule {
//  public:
//   HelloWorldModule(
//      modular::ModuleHost* module_host,
//      fidl::InterfaceRequest<views_v1::ViewProvider> view_provider_request) {}
//
//   // Called by ModuleDriver.
//   void Terminate(const std::function<void()>& done) { done(); }
// };
//
// int main(int argc, const char** argv) {
//   fsl::MessageLoop loop;
//   auto app_context = component::ApplicationContext::CreateFromStartupInfo();
//   modular::ModuleDriver<HelloWorldApp> driver(app_context.get(),
//                                               [&loop] { loop.QuitNow(); });
//   loop.Run();
//   return 0;
// }
template <typename Impl>
class ModuleDriver : LifecycleImpl::Delegate, ModuleHost {
 public:
  ModuleDriver(component::ApplicationContext* const app_context,
               std::function<void()> on_terminated)
      : app_context_(app_context),
        lifecycle_impl_(app_context->outgoing().deprecated_services(), this),
        on_terminated_(std::move(on_terminated)) {
    app_context_->ConnectToEnvironmentService(module_context_.NewRequest());

    // There is no guarantee that |ViewProvider| will be requested from us
    // before ModuleHost.set_view_provider_handler() is called from |Impl|, so
    // we buffer both events until they are both satisfied.
    app_context_->outgoing().AddPublicService<views_v1::ViewProvider>(
        [this](fidl::InterfaceRequest<views_v1::ViewProvider> request) {
          view_provider_request_ = std::move(request);
          InstantiateImpl();
        });
  }

 private:
  // |ModuleHost|
  component::ApplicationContext* application_context() override {
    return app_context_;
  }

  // |ModuleHost|
  ModuleContext* module_context() override {
    FXL_DCHECK(module_context_);
    return module_context_.get();
  }

  // |LifecycleImpl::Delegate|
  void Terminate() override {
    // It's possible that we process the |Lifecycle.Terminate| message before
    // the |Module.Initialize| message, even when both messages are ready to be
    // processed at the same time. In this case, because |impl_| hasn't been
    // instantiated yet, we cannot delegate the |Lifecycle.Terminate| message.
    if (impl_) {
      impl_->Terminate([this] {
        // Cf. AppDriver::Terminate().
        async::PostTask(async_get_default(), [this] {
            impl_.reset();
            on_terminated_();
        });
      });
    } else {
      on_terminated_();
    }
  }

  void InstantiateImpl() {
    impl_ = std::make_unique<Impl>(static_cast<ModuleHost*>(this),
                                   std::move(view_provider_request_));
  }

  component::ApplicationContext* const app_context_;
  LifecycleImpl lifecycle_impl_;
  std::function<void()> on_terminated_;
  ModuleContextPtr module_context_;

  // Only valid until |impl_| is instantiated.
  fidl::InterfaceRequest<views_v1::ViewProvider> view_provider_request_;

  std::unique_ptr<Impl> impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleDriver);
};

}  // namespace modular

#endif  // LIB_APP_DRIVER_CPP_MODULE_DRIVER_H_
