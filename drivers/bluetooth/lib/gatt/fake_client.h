// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/gatt/client.h"

namespace btlib {
namespace gatt {
namespace testing {

class FakeClient final : public Client {
 public:
  explicit FakeClient(async_t* dispatcher);
  ~FakeClient() override = default;

  void set_server_mtu(uint16_t mtu) { server_mtu_ = mtu; }
  void set_exchage_mtu_status(att::Status status) {
    exchange_mtu_status_ = status;
  }

  void set_primary_services(std::vector<ServiceData> services) {
    services_ = std::move(services);
  }

  void set_service_discovery_status(att::Status status) {
    service_discovery_status_ = status;
  }

 private:
  // Client overrides:
  fxl::WeakPtr<Client> AsWeakPtr() override;
  void ExchangeMTU(MTUCallback callback) override;
  void DiscoverPrimaryServices(ServiceCallback svc_callback,
                               StatusCallback status_callback) override;

  // All callbacks will be posted on this dispatcher to emulate asynchronous
  // behavior.
  async_t* dispatcher_;

  // Value to return for MTU exchange.
  uint16_t server_mtu_;

  // List of services to notify in DiscoverPrimaryServices.
  std::vector<ServiceData> services_;

  // Fake status values to return for GATT procedures.
  att::Status exchange_mtu_status_;
  att::Status service_discovery_status_;

  fxl::WeakPtrFactory<FakeClient> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeClient);
};

}  // namespace testing
}  // namespace gatt
}  // namespace btlib
