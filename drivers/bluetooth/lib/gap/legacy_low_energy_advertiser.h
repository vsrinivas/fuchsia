// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/gap/low_energy_advertiser.h"
#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace btlib {

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

  // LegacyLowEnergyAdvertiser supports only a single advertising instance,
  // hence it can report additional errors in the following conditions:
  // 1. If called while a start request is pending, reports
  //    hci::kRepeatedAttempts.
  // 2. If called while a stop request is pending, then cancels the stop request
  //    and proceeds with start.
  // TODO(armansito): We need to stop using HCI error codes for errors that are
  // not reported by the controller (NET-288).
  void StartAdvertising(const common::DeviceAddress& address,
                        const AdvertisingData& data,
                        const AdvertisingData& scan_rsp,
                        const ConnectionCallback& connect_callback,
                        uint32_t interval_ms,
                        bool anonymous,
                        const AdvertisingResultCallback& callback) override;

  // If called while a stop request is pending, returns false.
  // If called while a start request is pending, then cancels the start
  // request and proceeds with start.
  // Returns false if called while not advertising.
  bool StopAdvertising(const common::DeviceAddress& address) override;

  // Clears the advertising state before passing |connection| on to
  // |connect_callback_|.
  void OnIncomingConnection(LowEnergyConnectionRefPtr connection) override;

 private:
  // Unconditionally stops advertising.
  void StopAdvertisingInternal();

  // The transport that's used to issue commands
  fxl::RefPtr<hci::Transport> hci_;

  // |hci_cmd_runner_| will be running when a start or stop is pending.
  // |starting_| is set to true if a start is pending.
  bool starting_;
  std::unique_ptr<hci::SequentialCommandRunner> hci_cmd_runner_;

  // Non-zero if advertising has been enabled.
  common::DeviceAddress advertised_;

  // if not null, the callback for connectable advertising.
  ConnectionCallback connect_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LegacyLowEnergyAdvertiser);
};

}  // namespace gap

}  // namespace btlib
