// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_scanner.h"

#include "garnet/drivers/bluetooth/lib/hci/sequential_command_runner.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"
#include "lib/fxl/logging.h"

namespace btlib {
namespace hci {

// Default implementations do nothing.

void LowEnergyScanner::Delegate::OnDeviceFound(
    const LowEnergyScanResult& result,
    const common::ByteBuffer& data) {}

LowEnergyScanResult::LowEnergyScanResult()
    : connectable(false), rssi(hci::kRSSIInvalid) {}

LowEnergyScanResult::LowEnergyScanResult(const common::DeviceAddress& address,
                                         bool connectable,
                                         int8_t rssi)
    : address(address), connectable(connectable), rssi(rssi) {}

LowEnergyScanner::LowEnergyScanner(Delegate* delegate,
                                   fxl::RefPtr<Transport> hci,
                                   async_dispatcher_t* dispatcher)
    : state_(State::kIdle),
      delegate_(delegate),
      dispatcher_(dispatcher),
      transport_(hci) {
  FXL_DCHECK(delegate_);
  FXL_DCHECK(transport_);
  FXL_DCHECK(dispatcher_);

  hci_cmd_runner_ =
      std::make_unique<SequentialCommandRunner>(dispatcher_, transport_);
}

}  // namespace hci
}  // namespace btlib
