// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_MOJO_SINGLE_SERVICE_VIEW_APP_H_
#define APPS_MODULAR_MOJO_SINGLE_SERVICE_VIEW_APP_H_

#include "apps/mozart/services/views/interfaces/view_provider.mojom.h"
#include "apps/mozart/services/views/interfaces/view_token.mojom.h"
#include "lib/ftl/functional/make_copyable.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/connection_context.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/interface_request.h"

namespace modular {

// A common base class for applications that implement the
// |mozart::ViewProvider| service and then provide a *single* service via
// |mozart::ViewProvider::CreateView|.
// This is not a clean pattern.
// TODO(alhaad): Bug jeffbrown@ and abarth@ about it.
template <class Service, class ServiceImpl>
class SingleServiceViewApp : public mojo::ApplicationImplBase,
                             public mozart::ViewProvider {
 public:
  SingleServiceViewApp() {}
  ~SingleServiceViewApp() override {}

 private:
  // |mojo::ApplicationImplBase| override.
  bool OnAcceptConnection(mojo::ServiceProviderImpl* const s) override {
    s->AddService<mozart::ViewProvider>(
        [this](const mojo::ConnectionContext& ctx,
               mojo::InterfaceRequest<mozart::ViewProvider> request) {
          bindings_.AddBinding(this, std::move(request));
        });
    return true;
  }

  // |mozart::ViewProvider| override.
  void CreateView(
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> services) override {
    service_provider_impl_.Bind(mojo::ConnectionContext(), std::move(services));
    service_provider_impl_.AddService<Service>(ftl::MakeCopyable([
      this, view_owner_request = std::move(view_owner_request)
    ](const mojo::ConnectionContext& ctx,
      mojo::InterfaceRequest<Service> service_request) mutable {
      new ServiceImpl(mojo::CreateApplicationConnector(shell()),
                      std::move(service_request),
                      std::move(view_owner_request));
    }));
  }

  mojo::BindingSet<mozart::ViewProvider> bindings_;
  mojo::ServiceProviderImpl service_provider_impl_;
  FTL_DISALLOW_COPY_AND_ASSIGN(SingleServiceViewApp);
};

}  // namespace modular

#endif  // APPS_MODULAR_MOJO_SINGLE_SERVICE_VIEW_APP_H_
