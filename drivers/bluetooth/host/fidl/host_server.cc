// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "host_server.h"

#include "garnet/drivers/bluetooth/lib/gap/adapter.h"

#include "garnet/drivers/bluetooth/host/gatt_host.h"
#include "garnet/drivers/bluetooth/lib/gap/bredr_connection_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/bredr_discovery_manager.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_discovery_manager.h"
#include "garnet/drivers/bluetooth/lib/sm/util.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "helpers.h"
#include "low_energy_central_server.h"
#include "low_energy_peripheral_server.h"

namespace bthost {

using fuchsia::bluetooth::Bool;
using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Status;
using fuchsia::bluetooth::control::AdapterState;
using fuchsia::bluetooth::control::BondingData;
using fuchsia::bluetooth::control::Key;
using fuchsia::bluetooth::control::LEData;
using fuchsia::bluetooth::control::LTK;
using btlib::sm::IOCapability;

HostServer::HostServer(zx::channel channel,
                       fxl::WeakPtr<::btlib::gap::Adapter> adapter,
                       fbl::RefPtr<GattHost> gatt_host)
    : AdapterServerBase(adapter, this, std::move(channel)),
      pairing_delegate_(nullptr),
      gatt_host_(gatt_host),
      io_capability_(IOCapability::kNoInputNoOutput),
      weak_ptr_factory_(this) {
  FXL_DCHECK(gatt_host_);

  auto self = weak_ptr_factory_.GetWeakPtr();
  adapter->remote_device_cache()->set_device_updated_callback(
      [self](const auto& device) {
        if (self) {
          self->OnRemoteDeviceUpdated(device);
        }
      });
  adapter->remote_device_cache()->set_device_removed_callback(
      [self = weak_ptr_factory_.GetWeakPtr()](const auto& identifier) {
        if (self) {
          self->OnRemoteDeviceRemoved(identifier);
        }
      });
  adapter->remote_device_cache()->set_device_bonded_callback(
      [self = weak_ptr_factory_.GetWeakPtr()](const auto& device) {
        if (self) {
          self->OnRemoteDeviceBonded(device);
        }
      });
}

void HostServer::GetInfo(GetInfoCallback callback) {
  callback(fidl_helpers::NewAdapterInfo(*adapter()));
}

void HostServer::SetLocalName(::fidl::StringPtr local_name,
                              SetLocalNameCallback callback) {
  adapter()->SetLocalName(
      local_name, [self = weak_ptr_factory_.GetWeakPtr(),
                   callback = std::move(callback)](auto status) {
        callback(fidl_helpers::StatusToFidl(status, "Can't Set Local Name"));
      });
}

void HostServer::StartLEDiscovery(StartDiscoveryCallback callback) {
  auto le_manager = adapter()->le_discovery_manager();
  if (!le_manager) {
    callback(fidl_helpers::NewFidlError(ErrorCode::BAD_STATE,
                                        "Adapter is not initialized yet."));
    return;
  }
  le_manager->StartDiscovery([self = weak_ptr_factory_.GetWeakPtr(),
                              callback = std::move(callback)](auto session) {
    // End the new session if this AdapterServer got destroyed in the
    // mean time (e.g. because the client disconnected).
    if (!self) {
      callback(
          fidl_helpers::NewFidlError(ErrorCode::FAILED, "Adapter Shutdown"));
      return;
    }

    if (!session) {
      FXL_VLOG(1) << "Failed to start LE discovery session";
      callback(fidl_helpers::NewFidlError(
          ErrorCode::FAILED, "Failed to start LE discovery session"));
      self->bredr_discovery_session_ = nullptr;
      self->requesting_discovery_ = false;
      return;
    }

    // Set up a general-discovery filter for connectable devices.
    session->filter()->set_connectable(true);
    session->filter()->SetGeneralDiscoveryFlags();

    self->le_discovery_session_ = std::move(session);
    self->requesting_discovery_ = false;

    // Send the adapter state update.
    AdapterState state;
    state.discovering = Bool::New();
    state.discovering->value = true;
    self->binding()->events().OnAdapterStateChanged(std::move(state));

    callback(Status());
  });
}

void HostServer::StartDiscovery(StartDiscoveryCallback callback) {
  FXL_VLOG(1) << "Adapter StartDiscovery()";
  FXL_DCHECK(adapter());

  if (le_discovery_session_ || requesting_discovery_) {
    FXL_VLOG(1) << "Discovery already in progress";
    callback(fidl_helpers::NewFidlError(ErrorCode::IN_PROGRESS,
                                        "Discovery already in progress"));
    return;
  }

  requesting_discovery_ = true;
  auto bredr_manager = adapter()->bredr_discovery_manager();
  if (!bredr_manager) {
    StartLEDiscovery(std::move(callback));
    return;
  }
  // TODO(jamuraa): start these in parallel instead of sequence
  bredr_manager->RequestDiscovery(
      [self = weak_ptr_factory_.GetWeakPtr(), callback = std::move(callback)](
          btlib::hci::Status status, auto session) mutable {
        if (!self) {
          callback(fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                              "Adapter Shutdown"));
          return;
        }

        if (!status || !session) {
          FXL_VLOG(1) << "Failed to start BR/EDR discovery session";
          callback(fidl_helpers::StatusToFidl(
              status, "Failed to start BR/EDR discovery session"));
          self->requesting_discovery_ = false;
          return;
        }

        self->bredr_discovery_session_ = std::move(session);
        self->StartLEDiscovery(std::move(callback));
      });
}

void HostServer::StopDiscovery(StopDiscoveryCallback callback) {
  FXL_VLOG(1) << "Adapter StopDiscovery()";
  if (!le_discovery_session_) {
    FXL_VLOG(1) << "No active discovery session";
    callback(fidl_helpers::NewFidlError(ErrorCode::BAD_STATE,
                                        "No discovery session in progress"));
    return;
  }

  bredr_discovery_session_ = nullptr;
  le_discovery_session_ = nullptr;

  AdapterState state;
  state.discovering = Bool::New();
  state.discovering->value = false;
  this->binding()->events().OnAdapterStateChanged(std::move(state));

  callback(Status());
}

void HostServer::SetConnectable(bool connectable,
                                SetConnectableCallback callback) {
  FXL_VLOG(1) << "Adapter SetConnectable(" << connectable << ")";

  auto bredr_conn_manager = adapter()->bredr_connection_manager();
  if (!bredr_conn_manager) {
    callback(fidl_helpers::NewFidlError(ErrorCode::NOT_SUPPORTED,
                                        "Connectable mode not available"));
    return;
  }
  bredr_conn_manager->SetConnectable(
      connectable, [callback = std::move(callback)](const auto& status) {
        callback(fidl_helpers::StatusToFidl(status));
      });
}

void HostServer::AddBondedDevices(
    ::fidl::VectorPtr<fuchsia::bluetooth::control::BondingData> bonds,
    AddBondedDevicesCallback callback) {
  if (!bonds) {
    callback(fidl_helpers::NewFidlError(ErrorCode::NOT_SUPPORTED,
                                        "No bonds were added"));
    return;
  }

  for (auto& bond : *bonds) {
    // If LE Bond
    if (bond.le) {
      auto ltk = std::move(bond.le->ltk);
      auto security =
          fidl_helpers::NewSecurityLevel(ltk->key.security_properties);

      // Setup LTK to store
      btlib::common::UInt128 key_data;
      std::copy(ltk->key.value.begin(), ltk->key.value.begin() + 16,
                key_data.begin());
      auto link_key = btlib::hci::LinkKey(key_data, ltk->rand, ltk->ediv);
      auto store_ltk = btlib::sm::LTK(security, link_key);

      // Store the built ltk with the address
      auto addr = btlib::common::DeviceAddress(
          fidl_helpers::NewAddrType(bond.le->address_type), bond.le->address);
      auto resp = adapter()->AddBondedDevice(bond.identifier, addr, store_ltk);
      if (!resp) {
        callback(fidl_helpers::NewFidlError(
            ErrorCode::FAILED, "Devices were already present in cache"));
        return;
      }
    }
  }
  callback(Status());
}

void HostServer::OnRemoteDeviceBonded(
    const ::btlib::gap::RemoteDevice& remote_device) {
  FXL_VLOG(1) << "HostServer::OnRemoteDeviceBonded";
  BondingData data;
  data.identifier = remote_device.identifier().c_str();

  // If the bond is LE
  if (remote_device.technology() == btlib::gap::TechnologyType::kLowEnergy) {
    data.le = LEData::New();
    data.le->address = remote_device.address().value().ToString();

    if (remote_device.ltk()) {
      data.le->ltk = fuchsia::bluetooth::control::LTK::New();
      // Set security properties
      auto key_data = remote_device.ltk()->key().value().data();
      std::copy(key_data, key_data + 16, data.le->ltk->key.value.begin());
      data.le->ltk->key.security_properties.authenticated =
          remote_device.ltk()->security().authenticated();
      data.le->ltk->key.security_properties.secure_connections =
          remote_device.ltk()->security().secure_connections();
      data.le->ltk->key.security_properties.encryption_key_size =
          remote_device.ltk()->security().enc_key_size();

      data.le->ltk->key_size = remote_device.ltk()->security().enc_key_size();
      data.le->ltk->rand = remote_device.ltk()->key().rand();
      data.le->ltk->ediv = remote_device.ltk()->key().ediv();
    }
  }

  this->binding()->events().OnNewBondingData(std::move(data));
}

void HostServer::SetDiscoverable(bool discoverable,
                                 SetDiscoverableCallback callback) {
  FXL_VLOG(1) << "Adapter SetDiscoverable(" << discoverable << ")";
  // TODO(NET-830): advertise LE here
  if (!discoverable) {
    bredr_discoverable_session_ = nullptr;

    AdapterState state;
    state.discoverable = Bool::New();
    state.discoverable->value = false;
    this->binding()->events().OnAdapterStateChanged(std::move(state));

    callback(Status());
    return;
  }
  if (discoverable && requesting_discoverable_) {
    FXL_VLOG(1) << "Discoverable already being set";
    callback(fidl_helpers::NewFidlError(ErrorCode::IN_PROGRESS,
                                        "SetDiscoverable already in progress"));
    return;
  }
  requesting_discoverable_ = true;
  auto bredr_manager = adapter()->bredr_discovery_manager();
  if (!bredr_manager) {
    callback(fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                        "Discoverable mode not available"));
    return;
  }
  bredr_manager->RequestDiscoverable(
      [self = weak_ptr_factory_.GetWeakPtr(), callback = std::move(callback)](
          btlib::hci::Status status, auto session) {
        if (!self) {
          callback(fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                              "Adapter Shutdown"));
          return;
        }
        if (!status || !session) {
          FXL_VLOG(1) << "Failed to set discoverable";
          callback(
              fidl_helpers::StatusToFidl(status, "Failed to set discoverable"));
          self->requesting_discoverable_ = false;
        }
        self->bredr_discoverable_session_ = std::move(session);
        AdapterState state;
        state.discoverable = Bool::New();
        state.discoverable->value = true;
        self->binding()->events().OnAdapterStateChanged(std::move(state));
        callback(Status());
      });
}

void HostServer::RequestLowEnergyCentral(
    fidl::InterfaceRequest<fuchsia::bluetooth::le::Central> request) {
  BindServer<LowEnergyCentralServer>(std::move(request), gatt_host_);
}

void HostServer::RequestLowEnergyPeripheral(
    fidl::InterfaceRequest<fuchsia::bluetooth::le::Peripheral> request) {
  BindServer<LowEnergyPeripheralServer>(std::move(request));
}

void HostServer::RequestGattServer(
    fidl::InterfaceRequest<fuchsia::bluetooth::gatt::Server> request) {
  // GATT FIDL requests are handled by GattHost.
  gatt_host_->BindGattServer(std::move(request));
}

void HostServer::SetPairingDelegate(
    ::fuchsia::bluetooth::control::InputCapabilityType input,
    ::fuchsia::bluetooth::control::OutputCapabilityType output,
    ::fidl::InterfaceHandle<::fuchsia::bluetooth::control::PairingDelegate>
        delegate) {
  io_capability_ = fidl_helpers::NewIoCapability(input, output);

  auto self = weak_ptr_factory_.GetWeakPtr();
  pairing_delegate_.Bind(std::move(delegate));
  if (delegate) {
    adapter()->le_connection_manager()->SetPairingDelegate(self);
  } else {
    adapter()->le_connection_manager()->SetPairingDelegate(
        fxl::WeakPtr<PairingDelegate>());
  }
  pairing_delegate_.set_error_handler([self] {
    if (self) {
      self->adapter()->le_connection_manager()->SetPairingDelegate(
          fxl::WeakPtr<PairingDelegate>());
      FXL_VLOG(1) << "bt-host: Pairing Delegate disconnected";
    }
  });

}

void HostServer::Close() {
  FXL_VLOG(1) << "bthost: Closing FIDL handles";

  // Destroy all bindings.
  servers_.clear();
  gatt_host_->CloseServers();
}

btlib::sm::IOCapability HostServer::io_capability() const {
  FXL_VLOG(1) << "bthost: io capability: "
              << btlib::sm::util::IOCapabilityToString(io_capability_);
  return io_capability_;
}

void HostServer::StopPairing(std::string id, btlib::sm::Status status) {
  // TODO(armansito): implement
}

void HostServer::ConfirmPairing(std::string id, ConfirmCallback confirm) {
  FXL_LOG(INFO) << "bthost: Pairing request for device: " << id;
  // TODO(armansito): Call out to PairingDelegate FIDL interface
  confirm(true);
}

void HostServer::DisplayPasskey(std::string id, uint32_t passkey,
                                ConfirmCallback confirm) {
  FXL_LOG(INFO) << "bthost: Pairing request for device: " << id;

  // TODO(armansito): Call out to PairingDelegate FIDL interface
  FXL_LOG(INFO) << fxl::StringPrintf("bthost: Enter passkey: %06u", passkey);

  confirm(true);
}

void HostServer::RequestPasskey(std::string id,
                                PasskeyResponseCallback respond) {
  // TODO(armansito): Call out to PairingDelegate FIDL interface
  respond(-1);
}

void HostServer::OnConnectionError(Server* server) {
  FXL_DCHECK(server);
  servers_.erase(server);
}

void HostServer::OnRemoteDeviceUpdated(
    const ::btlib::gap::RemoteDevice& remote_device) {
  auto fidl_device = fidl_helpers::NewRemoteDevice(remote_device);
  if (!fidl_device) {
    FXL_VLOG(1) << "Ignoring malformed device update";
    return;
  }

  this->binding()->events().OnDeviceUpdated(std::move(*fidl_device));
}

void HostServer::OnRemoteDeviceRemoved(const std::string& identifier) {
  this->binding()->events().OnDeviceRemoved(identifier);
}

}  // namespace bthost
