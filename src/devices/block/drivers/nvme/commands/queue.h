// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_QUEUE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_QUEUE_H_

#include <hwreg/bitfields.h>

#include "src/devices/block/drivers/nvme/commands.h"

namespace nvme {

class CreateIoCompletionQueueSubmission : public Submission {
 public:
  static constexpr uint8_t kOpcode = 0x05;
  CreateIoCompletionQueueSubmission() : Submission(kOpcode) {}

  DEF_SUBFIELD(dword10, 31, 16, queue_size);
  DEF_SUBFIELD(dword10, 15, 0, queue_id);

  DEF_SUBFIELD(dword11, 31, 16, interrupt_vector);
  DEF_SUBBIT(dword11, 1, interrupt_en);
  DEF_SUBBIT(dword11, 0, contiguous);
};

class CreateIoSubmissionQueueSubmission : public Submission {
 public:
  static constexpr uint8_t kOpcode = 0x01;
  CreateIoSubmissionQueueSubmission() : Submission(kOpcode) {}

  DEF_SUBFIELD(dword10, 31, 16, queue_size);
  DEF_SUBFIELD(dword10, 15, 0, queue_id);

  DEF_SUBFIELD(dword11, 31, 16, completion_queue_id);
  // Only used for round-robin, which we don't support.
  DEF_SUBFIELD(dword11, 2, 1, queue_prio);
  DEF_SUBBIT(dword11, 0, contiguous);
};

class DeleteIoQueueSubmission : public Submission {
 public:
  static constexpr uint8_t kCompletionOpcode = 0x04;
  static constexpr uint8_t kSubmissionOpcode = 0x00;
  explicit DeleteIoQueueSubmission(bool is_completion)
      : Submission(is_completion ? kCompletionOpcode : kSubmissionOpcode) {}

  DEF_SUBFIELD(dword10, 15, 0, queue_id);
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_QUEUE_H_
