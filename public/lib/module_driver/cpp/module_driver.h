// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULE_DRIVER_CPP_MODULE_DRIVER_H_
#define LIB_MODULE_DRIVER_CPP_MODULE_DRIVER_H_

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/lifecycle/cpp/lifecycle_impl.h"
#include "lib/module/cpp/module_impl.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/module/fidl/module_context.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"

namespace modular {

// This interface is passed to the |Impl| object that ModuleDriver initializes.
class ModuleHost {
 public:
  virtual app::ApplicationContext* application_context() = 0;
  virtual ModuleContext* module_context() = 0;
};

// ModuleDriver provides a way to write modules and participate in application
// lifecycle. The |Impl| class supplied to ModuleDriver is instantiated when
// the Module and ViewProvider services are requested by the framework.
//
// Usage:
//   The |Impl| class must implement:
//
//      // A constructor with the following signature:
//      Constructor(
//           modular::ModuleHost* module_host,
//           fidl::InterfaceRequest<mozart::ViewProvider> view_provider_request,
//           fidl::InterfaceRequest<app::ServiceProvider> outgoing_services);
//
//   |outgoing_services| must contain the services that this module wants to
//   expose to the module that created it.
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
//      fidl::InterfaceRequest<mozart::ViewProvider> view_provider_request,
//      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) {}
//
//   // Called by ModuleDriver.
//   void Terminate(const std::function<void()>& done) { done(); }
// };
//
// int main(int argc, const char** argv) {
//   fsl::MessageLoop loop;
//   auto app_context = app::ApplicationContext::CreateFromStartupInfo();
//   modular::ModuleDriver<HelloWorldApp> driver(app_context.get(),
//                                               [&loop] { loop.QuitNow(); });
//   loop.Run();
//   return 0;
// }
template <typename Impl>
class ModuleDriver : LifecycleImpl::Delegate, ModuleImpl::Delegate, ModuleHost {
 public:
  ModuleDriver(app::ApplicationContext* const app_context,
               std::function<void()> on_terminated)
      : app_context_(app_context),
        lifecycle_impl_(app_context->outgoing_services(), this),
        module_impl_(std::make_unique<ModuleImpl>(
            app_context->outgoing_services(),
            static_cast<ModuleImpl::Delegate*>(this))),
        on_terminated_(std::move(on_terminated)) {
    // There is no guarantee that |ViewProvider| will be requested from us
    // before ModuleHost.set_view_provider_handler() is called from |Impl|, so
    // we buffer both events until they are both satisfied.
    app_context_->outgoing_services()->AddService<mozart::ViewProvider>(
        [this](fidl::InterfaceRequest<mozart::ViewProvider> request) {
          view_provider_request_ = std::move(request);
          MaybeInstantiateImpl();
        });
  }

 private:
  // |ModuleHost|
  app::ApplicationContext* application_context() override {
    return app_context_;
  }

  // |ModuleHost|
  ModuleContext* module_context() override {
    FXL_DCHECK(module_context_);
    return module_context_.get();
  }

  // |ModuleImpl::Delegate|
  void ModuleInit(
      fidl::InterfaceHandle<ModuleContext> module_context,
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) override {
    module_context_.Bind(std::move(module_context));
    outgoing_module_services_ = std::move(outgoing_services);
    MaybeInstantiateImpl();
  }

  // |LifecycleImpl::Delegate|
  void Terminate() override {
    // It's possible that we process the |Lifecycle.Terminate| message before
    // the |Module.Initialize| message, even when both messages are ready to be
    // processed at the same time. In this case, because |impl_| hasn't been
    // instantiated yet, we cannot delegate the |Lifecycle.Terminate| message.
    module_impl_.reset();
    if (impl_) {
      impl_->Terminate([this] {
        // Cf. AppDriver::Terminate().
        fsl::MessageLoop::GetCurrent()->task_runner()->PostTask([this] {
          impl_.reset();
          on_terminated_();
        });
      });
    } else {
      on_terminated_();
    }
  }

  void MaybeInstantiateImpl() {
    if (view_provider_request_ && module_context_) {
      impl_ = std::make_unique<Impl>(static_cast<ModuleHost*>(this),
                                     std::move(view_provider_request_),
                                     std::move(outgoing_module_services_));
    }
  }

  app::ApplicationContext* const app_context_;
  LifecycleImpl lifecycle_impl_;
  std::unique_ptr<ModuleImpl> module_impl_;
  std::function<void()> on_terminated_;
  ModuleContextPtr module_context_;

  // The following are only valid until |impl_| is instantiated.
  fidl::InterfaceRequest<mozart::ViewProvider> view_provider_request_;
  fidl::InterfaceRequest<app::ServiceProvider> outgoing_module_services_;

  std::unique_ptr<Impl> impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleDriver);
};

}  // namespace modular

#endif  // LIB_MODULE_DRIVER_CPP_MODULE_DRIVER_H_
