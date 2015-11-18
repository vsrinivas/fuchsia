// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/services/network/network_service_delegate.h"

#include "mojo/public/cpp/application/application_connection.h"

NetworkServiceDelegate::NetworkServiceDelegate() {}

NetworkServiceDelegate::~NetworkServiceDelegate() {}

void NetworkServiceDelegate::Initialize(mojo::ApplicationImpl* app) {
}

bool NetworkServiceDelegate::ConfigureIncomingConnection(
    mojo::ApplicationConnection* connection) {
  connection->AddService(this);
  return true;
}

void NetworkServiceDelegate::Quit() {
}

void NetworkServiceDelegate::Create(
    mojo::ApplicationConnection* connection,
    mojo::InterfaceRequest<mojo::NetworkService> request) {
  new mojo::NetworkServiceImpl(request.Pass(), connection);
}
