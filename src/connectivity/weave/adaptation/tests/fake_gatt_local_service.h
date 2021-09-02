// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_GATT_LOCAL_SERVICE_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_GATT_LOCAL_SERVICE_H_

#include <fuchsia/bluetooth/gatt/cpp/fidl_test_base.h>

#include <gtest/gtest.h>

namespace weave::adaptation::testing {

// Fake implementation of the fuchsia.bluetooth.gatt.LocalService
class FakeGATTLocalService : public fuchsia::bluetooth::gatt::testing::LocalService_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << name; }

  void RemoveService() override {
    binding_.Unbind();
    gatt_subscribe_confirmed_ = false;
  }

  void NotifyValue(uint64_t characteristic_id, std::string peer_id, std::vector<uint8_t> value,
                   bool confirm) override {
    gatt_subscribe_confirmed_ = true;
  }

  fidl::Binding<fuchsia::bluetooth::gatt::LocalService>& binding() { return binding_; }
  bool gatt_subscribe_confirmed() const { return gatt_subscribe_confirmed_; }

 private:
  fidl::Binding<fuchsia::bluetooth::gatt::LocalService> binding_{this};
  bool gatt_subscribe_confirmed_{false};
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_GATT_LOCAL_SERVICE_H_
