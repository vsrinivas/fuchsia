// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_NETWORK_SERVICE_IMPL_H_
#define MOJO_SERVICES_NETWORK_NETWORK_SERVICE_IMPL_H_

//#include "base/compiler_specific.h"
#include "base/macros.h"

#include "mojo/services/network/interfaces/network_service.mojom.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace mojo {
class ApplicationConnection;

class NetworkServiceImpl : public NetworkService {
 public:
  NetworkServiceImpl(InterfaceRequest<NetworkService> request,
                     ApplicationConnection* connection);
  ~NetworkServiceImpl() override;

  // NetworkService methods:
  void CreateURLLoader(InterfaceRequest<URLLoader> loader) override;

 private:
  StrongBinding<NetworkService> binding_;
};

}  // namespace mojo

#endif  // MOJO_SERVICES_NETWORK_NETWORK_SERVICE_IMPL_H_
