// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpt.h"

#include <stdio.h>
#include <zircon/assert.h>

#include <optional>
#include <vector>

#include "device_path.h"
#include "src/lib/utf_conversion/utf_conversion.h"
#include "utils.h"

namespace gigaboot {
namespace {
bool ValidateHeader(const gpt_header_t &header) {
  if (header.magic != GPT_MAGIC || header.size != GPT_HEADER_SIZE ||
      header.entries_size != GPT_ENTRY_SIZE || header.entries_count > 256) {
    return false;
  }

  // TODO(b/235489025): Implement checksum validation

  return true;
}

}  // namespace

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

fitx::result<efi_status> EfiGptBlockDevice::Load() {
  // First block is MBR. Read the second block for the GPT header.
  if (efi_status status = Read(&gpt_header_, BlockSize(), sizeof(gpt_header_t));
      status != EFI_SUCCESS) {
    return fitx::error(status);
  }

  if (!ValidateHeader(gpt_header_)) {
    // TODO(b/235489025): Implement checksum validation and backup gpt logic.
    return fitx::error(EFI_NOT_FOUND);
  }

  // Load all the partition entries

  // Allocate entries buffer
  entries_.resize(gpt_header_.entries_count);
  size_t offset = gpt_header_.entries * BlockSize();
  for (size_t i = 0; i < gpt_header_.entries_count; i++) {
    GptEntryInfo &new_entry = entries_[i];
    if (efi_status status = Read(&new_entry.entry, offset, gpt_header_.entries_size);
        status != EFI_SUCCESS) {
      return fitx::error(status);
    }

    // Pre-compute the utf8 name
    size_t dst_len = sizeof(new_entry.utf8_name);
    zx_status_t conv_status =
        utf16_to_utf8(reinterpret_cast<const uint16_t *>(new_entry.entry.name), GPT_NAME_LEN / 2,
                      reinterpret_cast<uint8_t *>(new_entry.utf8_name), &dst_len);
    if (conv_status != ZX_OK || dst_len > sizeof(new_entry.utf8_name)) {
      printf("Failed to convert partition name to utf8, %d, %zu\n", conv_status, dst_len);
      return fitx::error(EFI_UNSUPPORTED);
    }

    offset += gpt_header_.entries_size;
  }

  return fitx::ok();
}

efi_status EfiGptBlockDevice::Read(void *buffer, size_t offset, size_t length) {
  // According to UEFI specification chapter 13.7, disk-io protocol allows unaligned access.
  // Thus we don't check block alignment.
  return disk_io_protocol_->ReadDisk(disk_io_protocol_.get(), block_io_protocol_->Media->MediaId,
                                     offset, length, buffer);
}

efi_status EfiGptBlockDevice::Write(const void *data, size_t offset, size_t length) {
  // According to UEFI specification chapter 13.7, disk-io protocol allows unaligned access.
  // Thus we don't check block alignment.
  return disk_io_protocol_->WriteDisk(disk_io_protocol_.get(), block_io_protocol_->Media->MediaId,
                                      offset, length, data);
}

const gpt_entry_t *EfiGptBlockDevice::FindPartition(std::string_view name) {
  for (const GptEntryInfo &ele : GetGptEntries()) {
    const gpt_entry_t &entry = ele.entry;
    if (entry.first == 0 && entry.last == 0) {
      continue;
    }

    if (name == ele.utf8_name) {
      return &entry;
    }
  }

  return nullptr;
}

fitx::result<efi_status, size_t> EfiGptBlockDevice::CheckAndGetPartitionAccessRangeInStorage(
    std::string_view name, size_t offset, size_t length) {
  const gpt_entry_t *entry = FindPartition(name);
  if (!entry) {
    return fitx::error(EFI_NOT_FOUND);
  }

  size_t block_size = BlockSize();
  size_t abs_offset = entry->first * block_size + offset;
  if (abs_offset + length > (entry->last + 1) * block_size) {
    return fitx::error(EFI_INVALID_PARAMETER);
  }

  return fitx::ok(abs_offset);
}

fitx::result<efi_status> EfiGptBlockDevice::ReadPartition(std::string_view name, size_t offset,
                                                          size_t length, void *out) {
  auto res = CheckAndGetPartitionAccessRangeInStorage(name, offset, length);
  if (res.is_error()) {
    printf("ReadPartition: failed while checking and getting read range %s\n",
           EfiStatusToString(res.error_value()));
    return fitx::error(res.error_value());
  }

  efi_status status = Read(out, res.value(), length);
  if (status != EFI_SUCCESS) {
    return fitx::error(status);
  }

  return fitx::ok();
}

fitx::result<efi_status> EfiGptBlockDevice::WritePartition(std::string_view name, const void *data,
                                                           size_t offset, size_t length) {
  auto res = CheckAndGetPartitionAccessRangeInStorage(name, offset, length);
  if (res.is_error()) {
    printf("WritePartition: failed while checking and getting write range %s\n",
           EfiStatusToString(res.error_value()));
    return fitx::error(res.error_value());
  }

  efi_status status = Write(data, res.value(), length);
  if (status != EFI_SUCCESS) {
    return fitx::error(status);
  }

  return fitx::ok();
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
