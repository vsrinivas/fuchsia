// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme-cpp/fake/admin-commands.h"

#include <lib/ddk/debug.h>

#include "src/devices/block/drivers/nvme-cpp/commands.h"
#include "src/devices/block/drivers/nvme-cpp/commands/identify.h"
#include "src/devices/block/drivers/nvme-cpp/fake/fake-nvme-controller.h"

namespace fake_nvme {
using nvme::Completion;
using nvme::GenericStatus;
using nvme::StatusCodeType;

void DefaultAdminCommands::RegisterCommands(FakeNvmeController& controller) {
  controller.AddAdminCommand(nvme::IdentifySubmission::kOpcode, DefaultAdminCommands::Identify);
}

namespace {
void MakeIdentifyController(nvme::IdentifyController* out) {
  out->set_cqes_min_log2(__builtin_ctzl(sizeof(nvme::Completion)));
  out->set_sqes_min_log2(__builtin_ctzl(sizeof(nvme::Submission)));
  out->num_namespaces = 1;

  memset(out->serial_number, ' ', sizeof(out->serial_number));
  memset(out->model_number, ' ', sizeof(out->model_number));
  memset(out->firmware_rev, ' ', sizeof(out->firmware_rev));

  memcpy(out->serial_number, DefaultAdminCommands::kSerialNumber,
         strlen(DefaultAdminCommands::kSerialNumber));
  memcpy(out->model_number, DefaultAdminCommands::kModelNumber,
         strlen(DefaultAdminCommands::kModelNumber));
  memcpy(out->firmware_rev, DefaultAdminCommands::kFirmwareRev,
         strlen(DefaultAdminCommands::kFirmwareRev));
}
}  // namespace

void DefaultAdminCommands::Identify(nvme::Submission& default_submission,
                                    const nvme::TransactionData& data, Completion& completion) {
  using nvme::IdentifySubmission;
  completion.set_status_code_type(StatusCodeType::kGeneric)
      .set_status_code(GenericStatus::kSuccess);
  IdentifySubmission& submission = default_submission.GetSubmission<IdentifySubmission>();

  switch (submission.structure()) {
    case IdentifySubmission::IdentifyCns::kIdentifyController: {
      MakeIdentifyController(static_cast<nvme::IdentifyController*>(data.buffer.virt()));
      break;
    }
    default:
      zxlogf(ERROR, "unsuppoorted identify structure");
      completion.set_status_code(GenericStatus::kInvalidField);
      break;
  }
}

}  // namespace fake_nvme
