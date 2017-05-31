// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_device.h"

#include "apps/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/ftl/logging.h"

namespace bluetooth {
namespace gap {

RemoteDevice::RemoteDevice(const std::string& identifier, const common::DeviceAddress& address)
    : identifier_(identifier), address_(address) {
  FTL_DCHECK(!identifier_.empty());

  technology_ = (address_.type() == common::DeviceAddress::Type::kBREDR)
                    ? TechnologyType::kClassic
                    : TechnologyType::kLowEnergy;
}

void RemoteDevice::SetLowEnergyData(bool connectable, int8_t rssi,
                                    const common::ByteBuffer& advertising_data) {
  FTL_DCHECK(technology() == TechnologyType::kLowEnergy);
  FTL_DCHECK(address_.type() != common::DeviceAddress::Type::kBREDR);

  connectable_ = connectable;
  rssi_ = rssi;
  advertising_data_length_ = advertising_data.GetSize();

  // Reallocate the advertising data buffer if necessary.
  if (advertising_data_buffer_.GetSize() < advertising_data.GetSize()) {
    advertising_data_buffer_ =
        common::DynamicByteBuffer(advertising_data_length_, advertising_data.CopyContents());
  } else {
    // No need to reallocate. Re-use the existing buffer
    std::memcpy(advertising_data_buffer_.GetMutableData(), advertising_data.GetData(),
                advertising_data_length_);
  }
}

}  // namespace gap
}  // namespace bluetooth
