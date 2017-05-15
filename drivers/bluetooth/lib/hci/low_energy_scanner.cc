// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "low_energy_scanner.h"

#include "apps/bluetooth/lib/hci/sequential_command_runner.h"
#include "apps/bluetooth/lib/hci/transport.h"
#include "lib/ftl/logging.h"

namespace bluetooth {
namespace hci {

// Default implementations do nothing.

void LowEnergyScanner::Delegate::OnDeviceFound(const LowEnergyScanResult& result,
                                               const common::ByteBuffer& data) {}

LowEnergyScanResult::LowEnergyScanResult() : connectable(false), rssi(hci::kRSSIInvalid) {}

LowEnergyScanResult::LowEnergyScanResult(const common::DeviceAddress& address, bool connectable,
                                         int8_t rssi)
    : address(address), connectable(connectable), rssi(rssi) {}

LowEnergyScanner::LowEnergyScanner(Delegate* delegate, ftl::RefPtr<Transport> hci,
                                   ftl::RefPtr<ftl::TaskRunner> task_runner)
    : state_(State::kIdle), delegate_(delegate), task_runner_(task_runner), transport_(hci) {
  FTL_DCHECK(delegate_);
  FTL_DCHECK(transport_);
  FTL_DCHECK(task_runner_);

  hci_cmd_runner_ = std::make_unique<SequentialCommandRunner>(task_runner_, transport_);
}

}  // namespace hci
}  // namespace bluetooth
