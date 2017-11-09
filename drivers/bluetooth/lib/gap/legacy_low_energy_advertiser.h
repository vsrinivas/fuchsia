// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/gap/low_energy_advertiser.h"
#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace bluetooth {

namespace hci {
class Transport;
}

namespace gap {

class LegacyLowEnergyAdvertiser final : public LowEnergyAdvertiser {
 public:
  LegacyLowEnergyAdvertiser(fxl::RefPtr<hci::Transport> hci);
  ~LegacyLowEnergyAdvertiser() override;

  // LowEnergyAdvertiser overrides:
  size_t GetSizeLimit() override;
  size_t GetMaxAdvertisements() const override { return 1; }
  void StartAdvertising(const bluetooth::common::DeviceAddress& address,
                        const AdvertisingData& data,
                        const AdvertisingData& scan_rsp,
                        const ConnectionCallback& connect_callback,
                        uint32_t interval_ms,
                        bool anonymous,
                        const AdvertisingResultCallback& callback) override;
  bool StopAdvertising(
      const bluetooth::common::DeviceAddress& address) override;
  void OnIncomingConnection(LowEnergyConnectionRefPtr connection) override;

 private:
  // Unconditionally stops advertising.
  void StopAdvertisingInternal();

  // The transport that's used to issue commands
  fxl::RefPtr<hci::Transport> hci_;

  std::unique_ptr<hci::SequentialCommandRunner> hci_cmd_runner_;

  // Non-zero if advertising has been enabled.
  common::DeviceAddress advertised_;

  // if not null, the callback for connectable advertising.
  ConnectionCallback connect_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LegacyLowEnergyAdvertiser);
};

}  // namespace gap

}  // namespace bluetooth
