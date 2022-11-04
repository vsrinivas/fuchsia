// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_NVME_IO_H_
#define SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_NVME_IO_H_

#include "src/devices/block/drivers/nvme/commands.h"

namespace nvme {

class NvmIoSubmission : public Submission {
 public:
  constexpr static uint8_t kWriteOpcode = 0x01;
  constexpr static uint8_t kReadOpcode = 0x02;
  explicit NvmIoSubmission(bool is_write) : Submission(is_write ? kWriteOpcode : kReadOpcode) {}

 private:
  DEF_SUBFIELD(dword10, 31, 0, start_lba_lo);
  DEF_SUBFIELD(dword11, 31, 0, start_lba_hi);

 public:
  DEF_SUBBIT(dword12, 31, limited_retry);
  DEF_SUBBIT(dword12, 30, force_unit_access);
  DEF_SUBBIT(dword12, 24, storage_tag_check);
  DEF_SUBFIELD(dword12, 15, 0, block_count);

  uint64_t start_lba() const {
    return static_cast<uint64_t>(start_lba_hi()) << 32 | start_lba_lo();
  }
  NvmIoSubmission& set_start_lba(uint64_t lba) {
    set_start_lba_hi(lba >> 32);
    set_start_lba_lo(lba & 0xffff'ffff);
    return *this;
  }
};

}  // namespace nvme

#endif  // SRC_DEVICES_BLOCK_DRIVERS_NVME_COMMANDS_NVME_IO_H_
