// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A common base class for single service applications.

#ifndef APPS_MODULAR_APPLICATION_SINGLE_SERVICE_APPLICATION_H_
#define APPS_MODULAR_APPLICATION_SINGLE_SERVICE_APPLICATION_H_

#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace modular {

template <class Service, class ServiceImpl>
class SingleServiceApplication : public mojo::ApplicationImplBase {
 public:
  SingleServiceApplication() {}
  ~SingleServiceApplication() override {}

  bool OnAcceptConnection(mojo::ServiceProviderImpl* const s) override {
    s->AddService<Service>([this](const mojo::ConnectionContext& ctx,
                                  mojo::InterfaceRequest<Service> request) {
      service_impls_.emplace_back(std::unique_ptr<ServiceImpl>(new ServiceImpl(
          mojo::CreateApplicationConnector(shell()), request.Pass())));
    });
    return true;
  }

  void OnQuit() override { service_impls_.clear(); }

 private:
  std::vector<std::unique_ptr<ServiceImpl>> service_impls_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(SingleServiceApplication);
};

}  // namespace story

#endif  // APPS_MODULAR_APPLICATION_SINGLE_SERVICE_APPLICATION_H_
