// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_GPT_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_GPT_H_

#include <lib/fitx/result.h>

#include <optional>

#include "utils.h"

namespace gigaboot {

class EfiGptBlockDevice {
 public:
  static fitx::result<efi_status, EfiGptBlockDevice> Create(efi_handle device_handle);

  // No copy.
  EfiGptBlockDevice(const EfiGptBlockDevice&) = delete;
  EfiGptBlockDevice& operator=(const EfiGptBlockDevice&) = delete;

  EfiGptBlockDevice(EfiGptBlockDevice&&) = default;
  EfiGptBlockDevice& operator=(EfiGptBlockDevice&&) = default;

  // TODO(b/238334864): Add support for reading/writing GPT partitions.

  // TODO(b/238334864): Add support for initializing/updating GPT.

 private:
  // The parameters we need for reading/writing partitions live in both block and disk io protocols.
  EfiProtocolPtr<efi_block_io_protocol> block_io_protocol_;
  EfiProtocolPtr<efi_disk_io_protocol> disk_io_protocol_;

  EfiGptBlockDevice() {}
};

fitx::result<efi_status, EfiGptBlockDevice> FindEfiGptDevice();

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_GPT_H_
