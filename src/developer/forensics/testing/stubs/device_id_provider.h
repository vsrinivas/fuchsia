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

using DeviceIdProviderBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::feedback, DeviceIdProvider);

class DeviceIdProvider : public DeviceIdProviderBase {
 public:
  DeviceIdProvider(const std::string& device_id) : device_id_(device_id) {}

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 protected:
  const std::string& device_id() { return device_id_; }

 private:
  const std::string device_id_;
};

class DeviceIdProviderNeverReturns : public DeviceIdProviderBase {
 public:
  // |fuchsia::feedback::DeviceIdProvider|
  STUB_METHOD_DOES_NOT_RETURN(GetId, GetIdCallback);
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
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DEVICE_ID_PROVIDER_H_
