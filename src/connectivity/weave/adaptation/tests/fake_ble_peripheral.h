// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_BLE_PERIPHERAL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_BLE_PERIPHERAL_H_

#include <fuchsia/bluetooth/le/cpp/fidl_test_base.h>

#include <gtest/gtest.h>

namespace weave::adaptation::testing {

// Fake implementation of the fuchsia.bluetooth.le.Peripheral
class FakeBLEPeripheral : public fuchsia::bluetooth::le::testing::Peripheral_TestBase {
 public:
  void NotImplemented_(const std::string& name) override { FAIL() << name; }

  void StartAdvertising(fuchsia::bluetooth::le::AdvertisingParameters parameters,
                        fidl::InterfaceRequest<fuchsia::bluetooth::le::AdvertisingHandle> handle,
                        StartAdvertisingCallback callback) override {
    fuchsia::bluetooth::le::Peripheral_StartAdvertising_Response resp;
    fuchsia::bluetooth::le::Peripheral_StartAdvertising_Result result;
    result.set_response(resp);
    ASSERT_TRUE(handle.is_valid());
    adv_handle_ = std::move(handle);
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<fuchsia::bluetooth::le::Peripheral> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::bluetooth::le::Peripheral> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

 private:
  fidl::Binding<fuchsia::bluetooth::le::Peripheral> binding_{this};
  fidl::InterfaceRequest<fuchsia::bluetooth::le::AdvertisingHandle> adv_handle_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_BLE_PERIPHERAL_H_
