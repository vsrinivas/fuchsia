// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_NETWORK_REACHABILITY_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_NETWORK_REACHABILITY_PROVIDER_H_

#include <fuchsia/net/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>

#include "src/lib/fxl/logging.h"

namespace feedback {

class StubConnectivity : public fuchsia::net::Connectivity {
 public:
  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::net::Connectivity> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::net::Connectivity> request) {
      binding_ =
          std::make_unique<fidl::Binding<fuchsia::net::Connectivity>>(this, std::move(request));
    };
  }

  void TriggerOnNetworkReachable(bool reachable) {
    FXL_CHECK(binding_) << "No client is connected to the stub server yet";
    binding_->events().OnNetworkReachable(reachable);
  }

  void CloseConnection() {
    if (binding_) {
      binding_->Close(ZX_ERR_PEER_CLOSED);
    }
  }

 private:
  std::unique_ptr<fidl::Binding<fuchsia::net::Connectivity>> binding_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_NETWORK_REACHABILITY_PROVIDER_H_
