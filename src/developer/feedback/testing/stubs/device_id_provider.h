// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>

#include <memory>

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace stubs {

class DeviceIdProvider : public fuchsia::feedback::testing::DeviceIdProvider_TestBase {
 public:
  DeviceIdProvider(const std::string& device_id) : device_id_(device_id) {}

  fidl::InterfaceRequestHandler<fuchsia::feedback::DeviceIdProvider> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::feedback::DeviceIdProvider>>(
          this, std::move(request));
    };
  }

  void CloseConnection();

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

  // |fuchsia::feedback::testing::DeviceIdProvider_TestBase|
  void NotImplemented_(const std::string& name) override {
    FXL_NOTIMPLEMENTED() << name << " is not implemented";
  }

 protected:
  const std::string& device_id() { return device_id_; }

 private:
  std::string device_id_;
  std::unique_ptr<fidl::Binding<fuchsia::feedback::DeviceIdProvider>> binding_;
};

class DeviceIdProviderReturnsError : public DeviceIdProvider {
 public:
  DeviceIdProviderReturnsError() : DeviceIdProvider("") {}

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;
};

class DeviceIdProviderNeverReturns : public DeviceIdProvider {
 public:
  DeviceIdProviderNeverReturns(const std::string& device_id) : DeviceIdProvider(device_id) {}

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;
};

class DeviceIdProviderExpectsOneCall : public DeviceIdProvider {
 public:
  DeviceIdProviderExpectsOneCall(const std::string& device_id) : DeviceIdProvider(device_id) {}

  ~DeviceIdProviderExpectsOneCall();
  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 private:
  bool is_first_ = true;
};

class DeviceIdProviderClosesFirstConnection : public DeviceIdProvider {
 public:
  DeviceIdProviderClosesFirstConnection(const std::string& device_id)
      : DeviceIdProvider(device_id) {}

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 private:
  bool is_first_ = true;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_DEVICE_ID_PROVIDER_H_
