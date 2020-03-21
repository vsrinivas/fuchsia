// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/network_reachability_provider.h"

#include <zircon/errors.h>

#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace stubs {

void NetworkReachabilityProvider::TriggerOnNetworkReachable(const bool reachable) {
  FX_CHECK(binding_) << "No client is connected to the stub server yet";
  binding_->events().OnNetworkReachable(reachable);
}

void NetworkReachabilityProvider::CloseConnection() {
  if (binding_) {
    binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

}  // namespace stubs
}  // namespace feedback
