// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_FAKE_ADMIN_COMMANDS_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_FAKE_ADMIN_COMMANDS_H_

#include "src/devices/block/drivers/nvme-cpp/commands.h"
#include "src/devices/block/drivers/nvme-cpp/nvme.h"
#include "src/devices/block/drivers/nvme-cpp/queue-pair.h"

namespace fake_nvme {

// Default implementations of admin commands, that are sufficient to get a basic test passing.
class DefaultAdminCommands {
 public:
  constexpr static const char* kSerialNumber = "12345678";
  constexpr static const char* kModelNumber = "PL4T-1234";
  constexpr static const char* kFirmwareRev = "7.4.2.1";
  static void RegisterCommands(FakeNvmeController& controller);

 private:
  static void Identify(nvme::Submission& submission, const nvme::TransactionData& data,
                       nvme::Completion& completion);
};

}  // namespace fake_nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_CPP_FAKE_ADMIN_COMMANDS_H_
