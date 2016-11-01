// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_MOJO_SINGLE_SERVICE_VIEW_APP_H_
#define APPS_MODULAR_MOJO_SINGLE_SERVICE_VIEW_APP_H_

#include "apps/mozart/services/views/interfaces/view_provider.mojom.h"
#include "apps/mozart/services/views/interfaces/view_token.mojom.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/connection_context.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/interface_request.h"

namespace modular {

template <class Service, class ServiceImpl>
class ServiceProviderImpl : public mojo::ServiceProvider {
 public:
  ServiceProviderImpl(
      mojo::Shell* const shell,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> service_provider_request)
      : shell_(shell),
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
  void ConnectToService(const mojo::String& service_name,
                        mojo::ScopedMessagePipeHandle client_handle) override {
    if (service_name == Service::Name_) {
      new ServiceImpl(mojo::CreateApplicationConnector(shell_),
                      mojo::InterfaceRequest<Service>(std::move(client_handle)),
                      std::move(view_owner_request_));
    }
  }

  mojo::Shell* const shell_;
  mojo::StrongBinding<mojo::ServiceProvider> binding_;
  mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderImpl);
};

template <class Service, class ServiceImpl>
class ViewProviderImpl : public mozart::ViewProvider {
 public:
  ViewProviderImpl(mojo::Shell* const shell,
                   mojo::InterfaceRequest<mozart::ViewProvider> request)
      : shell_(shell), binding_(this, std::move(request)) {}

  ~ViewProviderImpl() override = default;

 private:
  void CreateView(mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  mojo::InterfaceRequest<mojo::ServiceProvider>
                      service_provider_request) override {
    new ServiceProviderImpl<Service, ServiceImpl>(
        shell_, std::move(view_owner_request),
        std::move(service_provider_request));
  }

  mojo::Shell* const shell_;
  mojo::StrongBinding<mozart::ViewProvider> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewProviderImpl);
};

// A common implementation for applications that implement the
// |mozart::ViewProvider| service and provide (possibly multiple
// instances of) a *single* service via
// |mozart::ViewProvider::CreateView|. The ServiceImpl instance is
// owned by its clients and is expected to delete itself eventually
// according to its service contract.
template <class Service, class ServiceImpl>
class SingleServiceViewApp : public mojo::ApplicationImplBase {
 public:
  SingleServiceViewApp() = default;

  ~SingleServiceViewApp() override = default;

 private:
  bool OnAcceptConnection(mojo::ServiceProviderImpl* const s) override {
    s->AddService<mozart::ViewProvider>([this](
        const mojo::ConnectionContext& ctx,
        mojo::InterfaceRequest<mozart::ViewProvider> request) {
      new ViewProviderImpl<Service, ServiceImpl>(shell(), std::move(request));
    });
    return true;
  }

  FTL_DISALLOW_COPY_AND_ASSIGN(SingleServiceViewApp);
};

}  // namespace modular

#endif  // APPS_MODULAR_MOJO_SINGLE_SERVICE_VIEW_APP_H_
