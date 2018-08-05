// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_ENVIRONMENT_HOST_MAXWELL_SERVICE_PROVIDER_BRIDGE_H_
#define PERIDOT_LIB_ENVIRONMENT_HOST_MAXWELL_SERVICE_PROVIDER_BRIDGE_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/svc/cpp/service_provider_bridge.h>

namespace maxwell {

// Environment surfacing only explicitly given environment services.
class MaxwellServiceProviderBridge : public component::ServiceProviderBridge {
 public:
  MaxwellServiceProviderBridge(fuchsia::sys::Environment* parent_env);
};

}  // namespace maxwell

#endif  // PERIDOT_LIB_ENVIRONMENT_HOST_MAXWELL_SERVICE_PROVIDER_BRIDGE_H_
