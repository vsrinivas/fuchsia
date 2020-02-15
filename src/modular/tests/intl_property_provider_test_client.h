// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_TESTS_INTL_PROPERTY_PROVIDER_TEST_CLIENT_H_
#define SRC_MODULAR_TESTS_INTL_PROPERTY_PROVIDER_TEST_CLIENT_H_

#include <fuchsia/intl/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_component.h>

#include <src/lib/fxl/logging.h>

namespace modular_tests {

// Simple client reused by various tests to ensure they can get i18n services from the environment.
class IntlPropertyProviderTestClient {
 public:
  IntlPropertyProviderTestClient(const modular_testing::FakeComponent* fake_component)
      : fake_component_(fake_component) {}

  zx_status_t Connect() {
    auto status = fake_component_->component_context()->svc()->Connect(client_.NewRequest());
    client_.set_error_handler([this](zx_status_t status) {
      FXL_LOG(ERROR) << "fuchsia::intl::PropertyProvider connection status: " << status;
      has_error_ = true;
    });
    return status;
  }

  void LoadProfile() {
    client_->GetProfile(
        [this](fuchsia::intl::Profile new_profile) { profile_ = std::move(new_profile); });
  }

  fuchsia::intl::Profile* Profile() { return HasProfile() ? &(profile_.value()) : nullptr; }

  bool HasProfile() const { return !!profile_; }
  bool HasError() const { return has_error_; }

 private:
  const modular_testing::FakeComponent* fake_component_;  // not owned
  fuchsia::intl::PropertyProviderPtr client_;
  std::optional<fuchsia::intl::Profile> profile_;
  bool has_error_ = false;
};

}  // namespace modular_tests

#endif  // SRC_MODULAR_TESTS_INTL_PROPERTY_PROVIDER_TEST_CLIENT_H_
