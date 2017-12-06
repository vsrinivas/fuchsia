// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/bluetooth/adapter_manager.h"
#include "garnet/drivers/bluetooth/lib/gatt/local_service_manager.h"
#include "garnet/drivers/bluetooth/lib/gatt/types.h"
#include "lib/bluetooth/fidl/gatt.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth_service {

// Implements the gatt::Server FIDL interface.
class GattServerFidlImpl : public ::bluetooth::gatt::Server,
                           public AdapterManager::Observer {
 public:
  // |adapter_manager| is used to lazily request a handle to the corresponding
  // adapter. It MUST out-live this GattServerFidlImpl instance.
  using ConnectionErrorHandler = std::function<void(GattServerFidlImpl*)>;
  GattServerFidlImpl(
      AdapterManager* adapter_manager,
      ::fidl::InterfaceRequest<::bluetooth::gatt::Server> request,
      const ConnectionErrorHandler& connection_error_handler);
  ~GattServerFidlImpl() override;

  // Removes the service with the given |id| if it is known.
  // This can be called as a result of FIDL connection errors (such as handle
  // closure) or as a result of gatt.Service.RemoveService().
  void RemoveService(uint64_t id);

 private:
  class ServiceImpl;

  // ::bluetooth::gatt::Server overrides:
  void PublishService(
      ::bluetooth::gatt::ServiceInfoPtr service_info,
      ::fidl::InterfaceHandle<::bluetooth::gatt::ServiceDelegate> delegate,
      ::fidl::InterfaceRequest<::bluetooth::gatt::Service> service_iface,
      const PublishServiceCallback& callback) override;

  // AdapterManager::Delegate overrides:
  void OnActiveAdapterChanged(::btlib::gap::Adapter* adapter) override;

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

  // We expect this to outlive us.
  AdapterManager* adapter_manager_;  // weak

  // The interface binding that represents the connection to the client
  // application.
  ::fidl::Binding<::bluetooth::gatt::Server> binding_;

  // The mapping between service identifiers and FIDL Service implementations
  // that they are bound to.
  // TODO(armansito): Consider using fbl::HashTable.
  std::unordered_map<uint64_t, std::unique_ptr<ServiceImpl>> services_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<GattServerFidlImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GattServerFidlImpl);
};

}  // namespace bluetooth_service
