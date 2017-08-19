// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_device.h"

#include "apps/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace bluetooth {
namespace gap {

RemoteDevice::RemoteDevice(const std::string& identifier, TechnologyType technology,
                           const common::DeviceAddress& address, bool connectable, bool temporary)
    : identifier_(identifier),
      technology_(technology),
      address_(address),
      connectable_(connectable),
      temporary_(temporary) {
  FXL_DCHECK(!identifier_.empty());
  FXL_DCHECK((address_.type() == common::DeviceAddress::Type::kBREDR) ==
             (technology_ == TechnologyType::kClassic));
}

void RemoteDevice::SetLowEnergyData(int8_t rssi, const common::ByteBuffer& advertising_data) {
  FXL_DCHECK(technology() == TechnologyType::kLowEnergy);
  FXL_DCHECK(address_.type() != common::DeviceAddress::Type::kBREDR);

  rssi_ = rssi;
  advertising_data_length_ = advertising_data.size();

  // Reallocate the advertising data buffer only if we need more space.
  if (advertising_data_buffer_.size() < advertising_data.size()) {
    advertising_data_buffer_ = common::DynamicByteBuffer(advertising_data.size());
  }

  advertising_data.Copy(&advertising_data_buffer_);
}

void RemoteDevice::SetLowEnergyConnectionData(const hci::Connection::LowEnergyParameters& params) {
  FXL_DCHECK(technology() == TechnologyType::kLowEnergy);
  FXL_DCHECK(address_.type() != common::DeviceAddress::Type::kBREDR);
  FXL_DCHECK(connectable());

  le_conn_params_ = params;
}

std::string RemoteDevice::ToString() const {
  return fxl::StringPrintf("{remote-device id: %s, address: %s}", identifier_.c_str(),
                           address_.ToString().c_str());
}

}  // namespace gap
}  // namespace bluetooth
