// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include "apps/bluetooth/lib/common/device_address.h"
#include "apps/bluetooth/lib/gap/low_energy_state.h"
#include "apps/bluetooth/lib/hci/acl_data_channel.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"
#include "lib/fxl/logging.h"

namespace bluetooth {
namespace gap {

// Stores controller settings and state information.
class AdapterState final {
 public:
  AdapterState();

  // The HCI version supported by the controller.
  hci::HCIVersion hci_version() const { return hci_version_; }

  // This returns Bluetooth Controller address. This address has the following meaning based on the
  // controller capabilities:
  //   - On BR/EDR this is the Bluetooth Controller Address, or BD_ADDR.
  //   - On LE this is the Public Device Address. This value can be used as the device's identity
  //     address. This value can be zero if a Public Device Address is not used.
  //   - On BR/EDR/LE this is the LE Public Device Address AND the BD_ADDR.
  const common::DeviceAddressBytes& controller_address() const { return controller_address_; }

  // Returns true if |feature_bit| is set as supported in the local LMP features list.
  inline bool HasLMPFeatureBit(size_t page, hci::LMPFeature feature_bit) const {
    FXL_DCHECK(page < 3);
    return lmp_features_[page] & static_cast<uint64_t>(feature_bit);
  }

  // Helpers for querying LMP capabilities.

  inline bool IsBREDRSupported() const {
    return !HasLMPFeatureBit(0u, hci::LMPFeature::kBREDRNotSupported);
  }

  inline bool IsLowEnergySupported() const {
    return HasLMPFeatureBit(0u, hci::LMPFeature::kLESupported);
  }

  // Returns true if |command_bit| in the given |octet| is set in the supported command list.
  inline bool IsCommandSupported(size_t octet, hci::SupportedCommand command_bit) const {
    FXL_DCHECK(octet < sizeof(supported_commands_));
    return supported_commands_[octet] & static_cast<uint8_t>(command_bit);
  }

  // Returns Bluetooth Low Energy specific state information.
  const LowEnergyState& low_energy_state() const { return le_state_; }

  // Returns the BR/EDR ACL data buffer capacity.
  const hci::DataBufferInfo& bredr_data_buffer_info() const { return bredr_data_buffer_info_; }

 private:
  // Let Adapter directly write to the private members.
  friend class Adapter;

  // The member variables in this class consist of controller settings that are shared between LE
  // and BR/EDR controllers. LE and BR/EDR specific state is stored in corresponding data
  // structures.
  // TODO(armansito): Actually do this and update the comment to refer to the variables.

  // HCI version supported by the controller.
  hci::HCIVersion hci_version_;

  // Supported LMP (Link Manager Protocol) features reported to us by the controller. LMP features
  // are organized into 3 "pages", each containing a bit-mask of supported controller features.
  // See Core Spec v5.0, Vol 2, Part C, Secton 3.3 "Feature Mask Definition".
  uint64_t lmp_features_[3];  // pages 0-2 of LMP features.

  // The maximum number of LMP feature pages that the controller has. Since only the first 3 pages
  // are specified, we use this to figure out whether or not to read page index 2.
  uint8_t max_lmp_feature_page_index_;

  // Bitmask list of HCI commands that the controller supports.
  uint8_t supported_commands_[64];

  // BD_ADDR (for classic) and Public Device Address (for LE).
  common::DeviceAddressBytes controller_address_;

  // The BR/EDR ACL data buffer size. We store this here as it is needed on dual-mode controllers
  // even if the host stack is compiled for LE-only.
  hci::DataBufferInfo bredr_data_buffer_info_;

  // BLE-specific state.
  LowEnergyState le_state_;

  // TODO(armansito): Add BREDRState class.
};

}  // namespace gap
}  // namespace bluetooth
