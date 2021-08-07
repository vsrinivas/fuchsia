// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_HWINFO_DEVICE_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_HWINFO_DEVICE_H_

#include <fuchsia/hwinfo/cpp/fidl_test_base.h>

#include <optional>

#include <gtest/gtest.h>

namespace weave::adaptation::testing {

// Fake implementation of the fuchsia.hwinfo.Device.
class FakeHwinfoDevice : public fuchsia::hwinfo::testing::Device_TestBase {
 public:
  // Default values for fuchsia.hwinfo.Device fields. For brevity, this only
  // includes the fields weavestack processes. However, all fields in this FIDL
  // table are expected to be set.
  static constexpr char kSerialNumber[] = "1234567890";

  // Replaces all unimplemented functions with a fatal error.
  void NotImplemented_(const std::string& name) override { FAIL() << name; }

  // Constructs a FakeHwinfoDevice using the provided configuration values.
  explicit FakeHwinfoDevice(std::optional<std::string> serial_number)
      : serial_number_(serial_number) {}

  // Constructs a FakeHwinfoDevice using the default configuration values.
  FakeHwinfoDevice() : FakeHwinfoDevice(kSerialNumber) {}

  // Returns the current DeviceInfo table.
  void GetInfo(fuchsia::hwinfo::Device::GetInfoCallback callback) override {
    fuchsia::hwinfo::DeviceInfo device_info;

    if (serial_number_) {
      device_info.set_serial_number(serial_number_.value());
    }

    callback(std::move(device_info));
  }

  // Returns an interface request handler to attach to a service directory.
  fidl::InterfaceRequestHandler<fuchsia::hwinfo::Device> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::hwinfo::Device> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  // Closes the binding, simulating the service going away.
  void Close(zx_status_t epitaph_value = ZX_OK) { binding_.Close(epitaph_value); }

  // Update the serial number.
  FakeHwinfoDevice& set_serial_number(std::optional<std::string> serial_number) {
    serial_number_ = serial_number;
    return *this;
  }

 private:
  fidl::Binding<fuchsia::hwinfo::Device> binding_{this};
  std::optional<std::string> serial_number_;
  std::optional<bool> is_retail_demo_;
  std::optional<std::string> retail_sku_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_HWINFO_DEVICE_H_
