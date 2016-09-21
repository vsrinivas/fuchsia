// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/network/network_service_delegate.h"

#include "mojo/public/cpp/application/service_provider_impl.h"

NetworkServiceDelegate::NetworkServiceDelegate() {}

NetworkServiceDelegate::~NetworkServiceDelegate() {}

void NetworkServiceDelegate::OnInitialize() {
}

bool NetworkServiceDelegate::OnAcceptConnection(
    mojo::ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<mojo::NetworkService>(
      [this](const mojo::ConnectionContext& connection_context,
             mojo::InterfaceRequest<mojo::NetworkService> request) {
        new mojo::NetworkServiceImpl(request.Pass());
      });
  return true;
}

void NetworkServiceDelegate::OnQuit() {
}

void NetworkServiceDelegate::Create(
    const mojo::ConnectionContext& connection,
    mojo::InterfaceRequest<mojo::NetworkService> request) {
  new mojo::NetworkServiceImpl(request.Pass());
}
