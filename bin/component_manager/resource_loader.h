// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_COMPONENT_MANAGER_RESOURCE_LOADER_H_
#define APPS_COMPONENT_MANAGER_RESOURCE_LOADER_H_

#include "lib/network/fidl/network_error.fidl.h"
#include "lib/network/fidl/network_service.fidl.h"

namespace component {

class ResourceLoader {
 public:
  using Callback = std::function<void(mx::vmo, network::NetworkErrorPtr)>;

  explicit ResourceLoader(network::NetworkServicePtr network_service);
  void LoadResource(const std::string& url, const Callback& callback_);

 private:
  network::NetworkServicePtr network_service_;
};

}  // namespace component

#endif  // APPS_COMPONENT_MANAGER_RESOURCE_LOADER_H_
