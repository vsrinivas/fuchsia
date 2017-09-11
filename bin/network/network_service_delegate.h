// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETWORK_NETWORK_SERVICE_DELEGATE_H_
#define APPS_NETWORK_NETWORK_SERVICE_DELEGATE_H_

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "garnet/bin/network/network_service_impl.h"

namespace network {

class NetworkServiceDelegate {
 public:
  NetworkServiceDelegate();
  ~NetworkServiceDelegate();

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  network::NetworkServiceImpl network_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetworkServiceDelegate);
};

}  // namespace network

#endif  // APPS_NETWORK_NETWORK_SERVICE_DELEGATE_H_
