// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_DRIVER_CPP_MODULE_DRIVER_H_
#define LIB_APP_DRIVER_CPP_MODULE_DRIVER_H_

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/logging.h>
#include <lib/lifecycle/cpp/lifecycle_impl.h>

namespace modular {

// This interface is passed to the |Impl| object that ModuleDriver initializes.
class ModuleHost {
 public:
  virtual fuchsia::sys::StartupContext* startup_context() = 0;
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
//           fidl::InterfaceRequest<fuchsia::ui::views_v1::ViewProvider>
//           view_provider_request);
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
//      fidl::InterfaceRequest<fuchsia::ui::views_v1::ViewProvider>
//      view_provider_request) {}
//
//   // Called by ModuleDriver.
//   void Terminate(const std::function<void()>& done) { done(); }
// };
//
// int main(int argc, const char** argv) {
//   async::Loop loop(&kAsyncLoopConfigMakeDefault);
//   auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
//   modular::ModuleDriver<HelloWorldApp> driver(context.get(),
//                                               [&loop] { loop.Quit(); });
//   loop.Run();
//   return 0;
// }
template <typename Impl>
class ModuleDriver : LifecycleImpl::Delegate, ModuleHost {
 public:
  ModuleDriver(fuchsia::sys::StartupContext* const context,
               std::function<void()> on_terminated)
      : context_(context),
        lifecycle_impl_(context->outgoing().deprecated_services(), this),
        on_terminated_(std::move(on_terminated)) {
    context_->ConnectToEnvironmentService(module_context_.NewRequest());

    context_->outgoing().AddPublicService<fuchsia::ui::views_v1::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::views_v1::ViewProvider>
                   request) {
          impl_ = std::make_unique<Impl>(static_cast<ModuleHost*>(this),
                                         std::move(request));
        });
  }

 private:
  // |ModuleHost|
  fuchsia::sys::StartupContext* startup_context() override { return context_; }

  // |ModuleHost|
  fuchsia::modular::ModuleContext* module_context() override {
    FXL_DCHECK(module_context_);
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
        async::PostTask(async_get_default(), [this] {
          impl_.reset();
          on_terminated_();
        });
      });
    } else {
      on_terminated_();
    }
  }

  fuchsia::sys::StartupContext* const context_;
  LifecycleImpl lifecycle_impl_;
  std::function<void()> on_terminated_;
  fuchsia::modular::ModuleContextPtr module_context_;

  std::unique_ptr<Impl> impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleDriver);
};

}  // namespace modular

#endif  // LIB_APP_DRIVER_CPP_MODULE_DRIVER_H_
