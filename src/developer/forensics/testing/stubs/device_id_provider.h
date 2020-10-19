// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl_test_base.h>

#include <memory>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

class DeviceIdProviderBase
    : public SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::feedback, DeviceIdProvider) {
 public:
  void SetDeviceId(std::string device_id);

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 protected:
  DeviceIdProviderBase() : device_id_(std::nullopt), callback_(nullptr) {}
  explicit DeviceIdProviderBase(const std::string& device_id)
      : device_id_(device_id), callback_(nullptr) {}

  void GetIdInternal(GetIdCallback callback);

 private:
  std::optional<std::string> device_id_;

  GetIdCallback callback_;
  bool dirty_{true};
};

class DeviceIdProvider : public DeviceIdProviderBase {
 public:
  explicit DeviceIdProvider(const std::string& device_id) : DeviceIdProviderBase(device_id) {}

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;
};

class DeviceIdProviderNeverReturns : public DeviceIdProviderBase {
 public:
  // |fuchsia::feedback::DeviceIdProvider|
  STUB_METHOD_DOES_NOT_RETURN(GetId, GetIdCallback);
};

class DeviceIdProviderExpectsOneCall : public DeviceIdProviderBase {
 public:
  explicit DeviceIdProviderExpectsOneCall(const std::string& device_id)
      : DeviceIdProviderBase(device_id) {}

  ~DeviceIdProviderExpectsOneCall();

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 private:
  bool is_first_ = true;
};

class DeviceIdProviderClosesFirstConnection : public DeviceIdProviderBase {
 public:
  DeviceIdProviderClosesFirstConnection(const std::string& device_id)
      : DeviceIdProviderBase(device_id) {}

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 private:
  bool is_first_ = true;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DEVICE_ID_PROVIDER_H_
