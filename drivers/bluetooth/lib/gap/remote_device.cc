// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_device.h"

#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace bluetooth {
namespace gap {

RemoteDevice::RemoteDevice(const std::string& identifier,
                           const common::DeviceAddress& address,
                           bool connectable)
    : identifier_(identifier),
      technology_((address.type() == common::DeviceAddress::Type::kBREDR)
                      ? TechnologyType::kClassic
                      : TechnologyType::kLowEnergy),
      address_(address),
      connectable_(connectable),
      temporary_(true),
      rssi_(hci::kRSSIInvalid),
      advertising_data_length_(0u) {
  FXL_DCHECK(!identifier_.empty());
  // TODO(armansito): Add a mechanism for assigning "dual-mode" for technology.
}

void RemoteDevice::SetLEAdvertisingData(
    int8_t rssi,
    const common::ByteBuffer& advertising_data) {
  FXL_DCHECK(technology() == TechnologyType::kLowEnergy);
  FXL_DCHECK(address_.type() != common::DeviceAddress::Type::kBREDR);

  rssi_ = rssi;
  advertising_data_length_ = advertising_data.size();

  // Reallocate the advertising data buffer only if we need more space.
  // TODO(armansito): Revisit this strategy while addressing NET-209
  if (advertising_data_buffer_.size() < advertising_data.size()) {
    advertising_data_buffer_ =
        common::DynamicByteBuffer(advertising_data_length_);
  }

  advertising_data.Copy(&advertising_data_buffer_);
}

bool RemoteDevice::TryMakeNonTemporary() {
  // TODO(armansito): Since we don't currently support address resolution,
  // random addresses should never be persisted.
  if (!connectable() ||
      address().type() == common::DeviceAddress::Type::kLERandom ||
      address().type() == common::DeviceAddress::Type::kLEAnonymous) {
    FXL_VLOG(1) << "gap: remains temporary: " << ToString();
    return false;
  }

  temporary_ = false;
  return true;
}

std::string RemoteDevice::ToString() const {
  return fxl::StringPrintf("{remote-device id: %s, address: %s}",
                           identifier_.c_str(), address_.ToString().c_str());
}

}  // namespace gap
}  // namespace bluetooth
