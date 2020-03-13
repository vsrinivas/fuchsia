// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_APP_DRIVER_CPP_MODULE_DRIVER_H_
#define SRC_MODULAR_LIB_APP_DRIVER_CPP_MODULE_DRIVER_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include <src/modular/lib/lifecycle/cpp/lifecycle_impl.h>

#include "src/lib/syslog/cpp/logger.h"

namespace modular {

// This interface is passed to the |Impl| object that ModuleDriver initializes.
class ModuleHost {
 public:
  virtual sys::ComponentContext* component_context() = 0;
  virtual fuchsia::modular::ModuleContext* module_context() = 0;
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
//           fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>
//           view_provider_request);
//
//       // Called by ModuleDriver. Call |done| once shutdown sequence is
//       // complete, at which point |this| will be deleted.
//       void Terminate(fit::function<void()> done);
//
// Example:
//
// class HelloWorldModule {
//  public:
//   HelloWorldModule(
//      modular::ModuleHost* module_host,
//      fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>
//      view_provider_request) {}
//
//   // Called by ModuleDriver.
//   void Terminate(fit::function<void()> done) { done(); }
// };
//
// int main(int argc, const char** argv) {
//   async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
//   auto context = sys::ComponentContext::Create();
//   modular::ModuleDriver<HelloWorldApp> driver(context.get(),
//                                               [&loop] { loop.Quit(); });
//   loop.Run();
//   return 0;
// }
template <typename Impl>
class ModuleDriver : LifecycleImpl::Delegate, ModuleHost {
 public:
  ModuleDriver(sys::ComponentContext* const context, fit::function<void()> on_terminated)
      : context_(context),
        lifecycle_impl_(context->outgoing(), this),
        on_terminated_(std::move(on_terminated)) {
    context_->svc()->Connect(module_context_.NewRequest());

    context_->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          impl_ = std::make_unique<Impl>(static_cast<ModuleHost*>(this), std::move(request));
        });
  }

 private:
  // |ModuleHost|
  sys::ComponentContext* component_context() override { return context_; }

  // |ModuleHost|
  fuchsia::modular::ModuleContext* module_context() override {
    FX_DCHECK(module_context_);
    return module_context_.get();
  }

  // |LifecycleImpl::Delegate|
  void Terminate() override {
    // It's possible that we process the |fuchsia::modular::Lifecycle.Terminate|
    // message before the |Module.Initialize| message, even when both messages
    // are ready to be processed at the same time. In this case, because |impl_|
    // hasn't been instantiated yet, we cannot delegate the
    // |fuchsia::modular::Lifecycle.Terminate| message.
    if (impl_) {
      impl_->Terminate([this] {
        // Cf. AppDriver::Terminate().
        async::PostTask(async_get_default_dispatcher(), [this] {
          impl_.reset();
          on_terminated_();
        });
      });
    } else {
      on_terminated_();
    }
  }

  sys::ComponentContext* const context_;
  LifecycleImpl lifecycle_impl_;
  fit::function<void()> on_terminated_;
  fuchsia::modular::ModuleContextPtr module_context_;

  std::unique_ptr<Impl> impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleDriver);
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_APP_DRIVER_CPP_MODULE_DRIVER_H_
