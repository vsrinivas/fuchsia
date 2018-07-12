// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"

namespace btlib {
namespace gatt {
namespace testing {

// This is a fake version of the root GATT object that can be injected in unit
// tests.
class FakeLayer final : public GATT {
 public:
  inline static fbl::RefPtr<FakeLayer> Create() {
    return fbl::AdoptRef(new FakeLayer());
  }

  // GATT overrides:
  void Initialize() override;
  void ShutDown() override;
  void AddConnection(const std::string& peer_id,
                     fbl::RefPtr<l2cap::Channel> att_chan) override;
  void RemoveConnection(std::string peer_id) override;
  void RegisterService(ServicePtr service,
                       ServiceIdCallback callback,
                       ReadHandler read_handler,
                       WriteHandler write_handler,
                       ClientConfigCallback ccc_callback) override;
  void UnregisterService(IdType service_id) override;
  void SendNotification(IdType service_id,
                        IdType chrc_id,
                        std::string peer_id,
                        ::fidl::VectorPtr<uint8_t> value,
                        bool indicate) override;
  void RegisterRemoteServiceWatcher(RemoteServiceWatcher callback,
                                    async_dispatcher_t* dispatcher) override;
  void ListServices(std::string peer_id,
                    std::vector<common::UUID> uuids,
                    ServiceListCallback callback) override;
  void FindService(std::string peer_id,
                   IdType service_id,
                   RemoteServiceCallback callback) override;

 private:
  friend class fbl::RefPtr<FakeLayer>;
  FakeLayer() = default;
  ~FakeLayer() override = default;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeLayer);
};

}  // namespace testing
}  // namespace gatt
}  // namespace btlib
