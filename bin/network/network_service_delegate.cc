// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network/network_service_delegate.h"

namespace network {

NetworkServiceDelegate::NetworkServiceDelegate(async_t* dispatcher)
    : context_(component::ApplicationContext::CreateFromStartupInfo()),
      network_provider_(dispatcher) {
  FXL_DCHECK(dispatcher),
  context_->outgoing_services()->AddService<NetworkService>(
      [this](fidl::InterfaceRequest<NetworkService> request) {
        network_provider_.AddBinding(std::move(request));
      });
}

NetworkServiceDelegate::~NetworkServiceDelegate() {}

}  // namespace network
