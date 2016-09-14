// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_NETWORK_SERVICE_DELEGATE_H_
#define MOJO_SERVICES_NETWORK_NETWORK_SERVICE_DELEGATE_H_

#include "apps/network/mojo/services/network/network_service_impl.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connection_context.h"

class NetworkServiceDelegate : public mojo::ApplicationImplBase {
 public:
  NetworkServiceDelegate();
  ~NetworkServiceDelegate() override;

 private:
  // mojo::ApplicationImplBase implementation.
  void OnInitialize() override;
  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override;
  void OnQuit() override;

  // Creates a content handler for the given connection (context and request).
  void Create(const mojo::ConnectionContext& connection,
              mojo::InterfaceRequest<mojo::NetworkService> request);

  FTL_DISALLOW_COPY_AND_ASSIGN(NetworkServiceDelegate);
};

#endif  // MOJO_SERVICES_NETWORK_NETWORK_SERVICE_DELEGATE_H_
