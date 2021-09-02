// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_GATT_SERVER_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_GATT_SERVER_H_

#include <fuchsia/bluetooth/gatt/cpp/fidl_test_base.h>

#include <gtest/gtest.h>

#include "fake_gatt_local_service.h"

namespace weave::adaptation::testing {

// Fake implementation of the fuchsia.bluetooth.gatt.Server
class FakeGATTService : public fuchsia::bluetooth::gatt::testing::Server_TestBase {
 public:
  static constexpr uint8_t kBtpConnectReqValue[] = {0x6E, 0x6C, 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x5};
  static constexpr uint64_t kCharacteristicId = 1;
  static constexpr char kPeerId[] = "123456";

  void NotImplemented_(const std::string& name) override { FAIL() << name; }

  void PublishService(
      fuchsia::bluetooth::gatt::ServiceInfo info,
      fidl::InterfaceHandle<fuchsia::bluetooth::gatt::LocalServiceDelegate> delegate,
      fidl::InterfaceRequest<fuchsia::bluetooth::gatt::LocalService> service,
      PublishServiceCallback callback) override {
    ::fuchsia::bluetooth::Status resp;
    local_service_.binding().Bind(std::move(service), gatt_server_dispatcher_);
    local_service_delegate_ = delegate.BindSync();
    callback(std::move(resp));
  }

  fidl::InterfaceRequestHandler<fuchsia::bluetooth::gatt::Server> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    gatt_server_dispatcher_ = dispatcher;
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::bluetooth::gatt::Server> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  fuchsia::bluetooth::gatt::ErrorCode WriteRequest() {
    fuchsia::bluetooth::gatt::ErrorCode out_status;
    local_service_delegate_->OnWriteValue(
        0, 0, std::vector<uint8_t>(std::begin(kBtpConnectReqValue), std::end(kBtpConnectReqValue)),
        &out_status);
    return out_status;
  }

  void OnCharacteristicConfiguration() {
    local_service_delegate_->OnCharacteristicConfiguration(kCharacteristicId, kPeerId,
                                                           false /* notify */, true /* indicate */);
  }

  bool WeaveConnectionConfirmed() const { return local_service_.gatt_subscribe_confirmed(); }

 private:
  fidl::Binding<fuchsia::bluetooth::gatt::Server> binding_{this};
  async_dispatcher_t* gatt_server_dispatcher_;
  FakeGATTLocalService local_service_;
  fidl::InterfaceHandle<class fuchsia::bluetooth::gatt::LocalServiceDelegate> delegate_;
  fuchsia::bluetooth::gatt::LocalServiceDelegateSyncPtr local_service_delegate_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_GATT_SERVER_H_
