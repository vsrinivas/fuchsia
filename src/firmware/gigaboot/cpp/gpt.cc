// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpt.h"

#include <stdio.h>
#include <zircon/assert.h>

#include <optional>

#include "device_path.h"
#include "utils.h"

namespace gigaboot {

fitx::result<efi_status, EfiGptBlockDevice> EfiGptBlockDevice::Create(efi_handle device_handle) {
  EfiGptBlockDevice ret;
  // Open the block IO protocol for this device.
  auto block_io = EfiOpenProtocol<efi_block_io_protocol>(device_handle);
  if (block_io.is_error()) {
    printf("Failed to open block io protocol %s\n", EfiStatusToString(block_io.error_value()));
    return fitx::error(block_io.error_value());
  }
  ret.block_io_protocol_ = std::move(block_io.value());

  // Open the disk IO protocol for this device.
  auto disk_io = EfiOpenProtocol<efi_disk_io_protocol>(device_handle);
  if (disk_io.is_error()) {
    printf("Failed to open disk io protocol %s\n", EfiStatusToString(disk_io.error_value()));
    return fitx::error(disk_io.error_value());
  }
  ret.disk_io_protocol_ = std::move(disk_io.value());

  return fitx::ok(std::move(ret));
}

// TODO(https://fxbug.dev/79197): The function currently only finds the storage devie that hosts
// the currently running image. This can be a problem when booting from USB. Add support to handle
// the USB case.
fitx::result<efi_status, EfiGptBlockDevice> FindEfiGptDevice() {
  auto image_device_path = EfiOpenProtocol<efi_device_path_protocol>(gEfiLoadedImage->DeviceHandle);
  if (image_device_path.is_error()) {
    printf("Failed to open device path protocol %s\n",
           EfiStatusToString(image_device_path.error_value()));
    return fitx::error{image_device_path.error_value()};
  }

  // Find all handles that support block io protocols.
  auto block_io_supported_handles = EfiLocateHandleBufferByProtocol<efi_block_io_protocol>();
  if (block_io_supported_handles.is_error()) {
    printf("Failed to locate handles supporting block io protocol\n");
    return fitx::error(block_io_supported_handles.error_value());
  }

  // Scan all handles and find the one from which the currently running image comes.
  // This is done by checking if they share common device path prefix.
  for (auto handle : block_io_supported_handles->AsSpan()) {
    auto block_io = EfiOpenProtocol<efi_block_io_protocol>(handle);
    if (block_io.is_error()) {
      printf("Failed to open block io protocol\n");
      return fitx::error(block_io.error_value());
    }

    // Skip logical partition blocks and non present devices.
    efi_block_io_protocol *bio = block_io.value().get();
    if (bio->Media->LogicalPartition || !bio->Media->MediaPresent) {
      continue;
    }

    // Check device path prefix match.
    auto device_path = EfiOpenProtocol<efi_device_path_protocol>(handle);
    if (device_path.is_error()) {
      printf("Failed to create device path protocol\n");
      return fitx::error(device_path.error_value());
    }

    if (EfiDevicePathNode::StartsWith(image_device_path.value().get(), device_path.value().get())) {
      // Open the disk io protocol
      auto efi_gpt_device = EfiGptBlockDevice::Create(handle);
      if (efi_gpt_device.is_error()) {
        printf("Failed to create GPT device\n");
        return fitx::error(efi_gpt_device.error_value());
      }

      return fitx::ok(std::move(efi_gpt_device.value()));
    }
  }

  printf("No matching block device found\n");
  return fitx::error{EFI_NOT_FOUND};
}

}  // namespace gigaboot
