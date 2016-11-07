// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_MOJO_SINGLE_SERVICE_VIEW_APP_H_
#define APPS_MODULAR_MOJO_SINGLE_SERVICE_VIEW_APP_H_

#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/mojo/strong_binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

namespace single_service_view_app {

template <class Service, class ServiceImpl>
class ServiceProviderImpl : public ServiceProvider {
 public:
  ServiceProviderImpl(
      std::shared_ptr<ApplicationContext> application_context,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<ServiceProvider> service_provider_request)
      : application_context_(application_context),
        binding_(this, std::move(service_provider_request)),
        view_owner_request_(std::move(view_owner_request)) {}

  ~ServiceProviderImpl() override = default;

 private:
  // TODO/HACK(mesch): After calling CreateView() with a view owner
  // request, the ConnectToService() can be called at most once,
  // because it uses up the view owner request. So the whole
  // ServiceProvider instance should be used exactly once, and a new
  // one created for the next request. This is what story runner does,
  // but the question remains why we have two separate calls on two
  // separate service instances to pass two parameters for the *same*
  // constructor invocation.
  //
  // Presumably, if ConnectToService() is called multiple times, it
  // should always connect to the same service instance, but we
  // neither need nor support this right now.
  void ConnectToService(const fidl::String& service_name,
                        mx::channel pipe) override {
    if (service_name == Service::Name_) {
      new ServiceImpl(application_context_
                          ->ConnectToEnvironmentService<mozart::ViewManager>(),
                      fidl::InterfaceRequest<Service>(std::move(pipe)),
                      std::move(view_owner_request_));
    }
  }

  std::shared_ptr<ApplicationContext> application_context_;
  StrongBinding<ServiceProvider> binding_;
  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderImpl);
};

template <class Service, class ServiceImpl>
class ViewProviderImpl : public mozart::ViewProvider {
 public:
  ViewProviderImpl(std::shared_ptr<ApplicationContext> application_context,
                   fidl::InterfaceRequest<mozart::ViewProvider> request)
      : application_context_(application_context),
        binding_(this, std::move(request)) {}

  ~ViewProviderImpl() override = default;

 private:
  void CreateView(fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  fidl::InterfaceRequest<ServiceProvider>
                      service_provider_request) override {
    new ServiceProviderImpl<Service, ServiceImpl>(
        application_context_, std::move(view_owner_request),
        std::move(service_provider_request));
  }

  std::shared_ptr<modular::ApplicationContext> application_context_;
  StrongBinding<mozart::ViewProvider> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewProviderImpl);
};

}  // namespace single_service_view_app

// A common implementation for applications that implement the
// |mozart::ViewProvider| service and provide (possibly multiple
// instances of) a *single* service via
// |mozart::ViewProvider::CreateView|. The ServiceImpl instance is
// owned by its clients and is expected to delete itself eventually
// according to its service contract.
template <class Service, class ServiceImpl>
class SingleServiceViewApp {
 public:
  SingleServiceViewApp()
      : application_context_(ApplicationContext::CreateFromStartupInfo()) {
    application_context_->outgoing_services()->AddService<mozart::ViewProvider>(
        [this](fidl::InterfaceRequest<mozart::ViewProvider> request) {
          new single_service_view_app::ViewProviderImpl<Service, ServiceImpl>(
              application_context_, std::move(request));
        });
  }

 private:
  std::shared_ptr<modular::ApplicationContext> application_context_;
  FTL_DISALLOW_COPY_AND_ASSIGN(SingleServiceViewApp);
};

}  // namespace modular

#endif  // APPS_MODULAR_MOJO_SINGLE_SERVICE_VIEW_APP_H_
