// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_SINGLE_SERVICE_APP_H_
#define PERIDOT_LIB_FIDL_SINGLE_SERVICE_APP_H_

#include <memory>

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/macros.h>

namespace modular {

// Base class for a simple application which provides a single instance of a
// single service and the ViewProvider service. It also implements a Terminate()
// method that makes it suitable to be used as an Impl class of AppDriver.
template <class Service>
class SingleServiceApp : protected Service,
                         private fuchsia::ui::views_v1::ViewProvider {
 public:
  SingleServiceApp(component::StartupContext* const startup_context)
      : startup_context_(startup_context),
        service_binding_(new fidl::Binding<Service>(this)),
        view_provider_binding_(this) {
    startup_context_->outgoing().AddPublicService<Service>(
        [this](fidl::InterfaceRequest<Service> request) {
          FXL_DCHECK(!service_binding_->is_bound());
          service_binding_->Bind(std::move(request));
        });
    startup_context_->outgoing()
        .AddPublicService<fuchsia::ui::views_v1::ViewProvider>(
            [this](fidl::InterfaceRequest<fuchsia::ui::views_v1::ViewProvider>
                       request) {
              FXL_DCHECK(!view_provider_binding_.is_bound());
              view_provider_binding_.Bind(std::move(request));
            });
  }

  ~SingleServiceApp() override = default;

  virtual void Terminate(std::function<void()> done) { done(); }

 protected:
  component::StartupContext* startup_context() const {
    return startup_context_;
  }

 private:
  // |ViewProvider| -- Derived classes may override this method.
  void CreateView(
      fidl::InterfaceRequest<
          fuchsia::ui::views_v1_token::ViewOwner> /*view_owner_request*/,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*services*/)
      override {}

  component::StartupContext* const startup_context_;
  std::unique_ptr<fidl::Binding<Service>> service_binding_;
  fidl::Binding<fuchsia::ui::views_v1::ViewProvider> view_provider_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SingleServiceApp);
};

// Base class for a simple application which only provides the ViewProvider
// service. It also implements a Terminate() method that makes it suitable to be
// used as an Impl class of AppDriver.
class ViewApp : private fuchsia::ui::views_v1::ViewProvider {
 public:
  ViewApp(component::StartupContext* const startup_context)
      : startup_context_(startup_context), view_provider_binding_(this) {
    startup_context_->outgoing()
        .AddPublicService<fuchsia::ui::views_v1::ViewProvider>(
            [this](fidl::InterfaceRequest<fuchsia::ui::views_v1::ViewProvider>
                       request) {
              FXL_DCHECK(!view_provider_binding_.is_bound());
              view_provider_binding_.Bind(std::move(request));
            });
  }

  ~ViewApp() override = default;

  virtual void Terminate(std::function<void()> done) { done(); }

 protected:
  component::StartupContext* startup_context() const {
    return startup_context_;
  }

 private:
  // |ViewProvider| -- Derived classes may override this method.
  void CreateView(
      fidl::InterfaceRequest<
          fuchsia::ui::views_v1_token::ViewOwner> /*view_owner_request*/,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*services*/)
      override {}

  component::StartupContext* const startup_context_;
  fidl::Binding<fuchsia::ui::views_v1::ViewProvider> view_provider_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewApp);
};

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_SINGLE_SERVICE_APP_H_
