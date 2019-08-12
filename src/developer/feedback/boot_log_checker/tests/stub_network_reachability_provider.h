// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_NETWORK_REACHABILITY_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_NETWORK_REACHABILITY_PROVIDER_H_

#include <fuchsia/net/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>

namespace feedback {

class StubConnectivity : public fuchsia::net::Connectivity {
 public:
  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::net::Connectivity> GetHandler() {
    return bindings_.GetHandler(this);
  }

  void TriggerOnNetworkReachable(bool reachable) {
    for (const auto& binding : bindings_.bindings()) {
      binding->events().OnNetworkReachable(reachable);
    }
  }

  void CloseAllConnections() { bindings_.CloseAll(); }

 private:
  fidl::BindingSet<fuchsia::net::Connectivity> bindings_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_NETWORK_REACHABILITY_PROVIDER_H_
