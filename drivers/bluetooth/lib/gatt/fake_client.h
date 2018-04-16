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

  void set_characteristics(std::vector<CharacteristicData> chrcs) {
    chrcs_ = std::move(chrcs);
  }

  void set_characteristic_discovery_status(att::Status status) {
    chrc_discovery_status_ = status;
  }

  att::Handle last_chrc_discovery_start_handle() const {
    return last_chrc_discovery_start_handle_;
  }

  att::Handle last_chrc_discovery_end_handle() const {
    return last_chrc_discovery_end_handle_;
  }

  size_t chrc_discovery_count() const { return chrc_discovery_count_; }

  // Sets a callback which will run when WriteRequest gets called.
  using WriteRequestCallback = std::function<
      void(att::Handle, const common::ByteBuffer&, att::StatusCallback)>;
  void set_write_request_callback(WriteRequestCallback callback) {
    write_request_callback_ = std::move(callback);
  }

 private:
  // Client overrides:
  fxl::WeakPtr<Client> AsWeakPtr() override;
  void ExchangeMTU(MTUCallback callback) override;
  void DiscoverPrimaryServices(ServiceCallback svc_callback,
                               att::StatusCallback status_callback) override;
  void DiscoverCharacteristics(att::Handle range_start,
                               att::Handle range_end,
                               CharacteristicCallback chrc_callback,
                               att::StatusCallback status_callback) override;
  void DiscoverDescriptors(att::Handle range_start,
                           att::Handle range_end,
                           DescriptorCallback desc_callback,
                           att::StatusCallback status_callback) override;
  void WriteRequest(att::Handle handle,
                    const common::ByteBuffer& value,
                    att::StatusCallback callback) override;

  // All callbacks will be posted on this dispatcher to emulate asynchronous
  // behavior.
  async_t* dispatcher_;

  // Value to return for MTU exchange.
  uint16_t server_mtu_ = att::kLEMinMTU;

  // Data used for DiscoveryPrimaryServices().
  std::vector<ServiceData> services_;

  // Fake status values to return for GATT procedures.
  att::Status exchange_mtu_status_;
  att::Status service_discovery_status_;
  att::Status chrc_discovery_status_;

  // Data used for DiscoverCharacteristics().
  std::vector<CharacteristicData> chrcs_;
  att::Handle last_chrc_discovery_start_handle_ = 0;
  att::Handle last_chrc_discovery_end_handle_ = 0;
  size_t chrc_discovery_count_ = 0;

  // Called by WriteRequest().
  WriteRequestCallback write_request_callback_;

  fxl::WeakPtrFactory<FakeClient> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeClient);
};

}  // namespace testing
}  // namespace gatt
}  // namespace btlib
