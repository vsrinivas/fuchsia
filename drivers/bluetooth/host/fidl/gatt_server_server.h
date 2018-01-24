// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/bluetooth/fidl/gatt.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/host/fidl/server_base.h"
#include "garnet/drivers/bluetooth/lib/gatt/local_service_manager.h"
#include "garnet/drivers/bluetooth/lib/gatt/types.h"

namespace bthost {

// Implements the gatt::Server FIDL interface.
class GattServerServer : public ServerBase<bluetooth::gatt::Server> {
 public:
  // |adapter_manager| is used to lazily request a handle to the corresponding
  // adapter. It MUST out-live this GattServerServer instance.
  GattServerServer(fxl::WeakPtr<::btlib::gap::Adapter> adapter,
                   fidl::InterfaceRequest<bluetooth::gatt::Server> request);

  ~GattServerServer() override;

  // Removes the service with the given |id| if it is known.
  // This can be called as a result of FIDL connection errors (such as handle
  // closure) or as a result of gatt.Service.RemoveService().
  void RemoveService(uint64_t id);

 private:
  class ServiceImpl;

  // ::bluetooth::gatt::Server overrides:
  void PublishService(
      bluetooth::gatt::ServiceInfoPtr service_info,
      fidl::InterfaceHandle<bluetooth::gatt::ServiceDelegate> delegate,
      fidl::InterfaceRequest<bluetooth::gatt::Service> service_iface,
      const PublishServiceCallback& callback) override;

  // Called when a remote device issues a read request to one of our services.
  void OnReadRequest(::btlib::gatt::IdType service_id,
                     ::btlib::gatt::IdType id,
                     uint16_t offset,
                     const ::btlib::gatt::ReadResponder& responder);

  // Called when a remote device issues a write request to one of our services.
  void OnWriteRequest(::btlib::gatt::IdType service_id,
                      ::btlib::gatt::IdType id,
                      uint16_t offset,
                      const ::btlib::common::ByteBuffer& value,
                      const ::btlib::gatt::WriteResponder& responder);

  // Called when a remote device has configured notifications or indications on
  // a local characteristic.
  void OnCharacteristicConfig(::btlib::gatt::IdType service_id,
                              ::btlib::gatt::IdType chrc_id,
                              const std::string& peer_id,
                              bool notify,
                              bool indicate);

  // The mapping between service identifiers and FIDL Service implementations.
  // TODO(armansito): Consider using fbl::HashTable.
  std::unordered_map<uint64_t, std::unique_ptr<ServiceImpl>> services_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<GattServerServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GattServerServer);
};

}  // namespace bthost
