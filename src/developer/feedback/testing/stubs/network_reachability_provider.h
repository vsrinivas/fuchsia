// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_NETWORK_REACHABILITY_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_NETWORK_REACHABILITY_PROVIDER_H_

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace stubs {

class NetworkReachabilityProvider : public fuchsia::net::testing::Connectivity_TestBase {
 public:
  fidl::InterfaceRequestHandler<fuchsia::net::Connectivity> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::net::Connectivity> request) {
      binding_ =
          std::make_unique<fidl::Binding<fuchsia::net::Connectivity>>(this, std::move(request));
    };
  }

  void CloseConnection();

  // |fuchsia::net::Connectivity|
  void TriggerOnNetworkReachable(bool reachable);

  // |fuchsia::net::testing::Connectivity_TestBase|
  void NotImplemented_(const std::string& name) override {
    FXL_NOTIMPLEMENTED() << name << " is not implemented";
  }

 private:
  std::unique_ptr<fidl::Binding<fuchsia::net::Connectivity>> binding_;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_NETWORK_REACHABILITY_PROVIDER_H_
