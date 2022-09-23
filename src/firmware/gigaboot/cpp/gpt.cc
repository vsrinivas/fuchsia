// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpt.h"

#include <lib/cksum.h>
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

  // The header crc is in the middle of the structure, and
  // as per the spec is zeroed before the crc is calculated.
  // An easy way to make that calculation without modifying the header
  // is to make a copy, zero out its crc, and calculate the checksum on the copy.
  gpt_header_t copy(header);
  copy.crc32 = 0;
  copy.crc32 = crc32(0, reinterpret_cast<uint8_t *>(&copy), sizeof(copy));

  return copy.crc32 == header.crc32;
}

void RestoreHeaderFromGood(const gpt_header_t &good, gpt_header_t *damaged) {
  *damaged = good;

  damaged->backup = good.current;
  damaged->current = good.backup;

  // This is an unfortunate hack: for every other entry in the header,
  // it doesn't matter whether the damaged header is the primary or the backup,
  // and similarly for the good header.
  damaged->entries = (damaged->current == 1) ? 2 : damaged->last + 1;

  damaged->crc32 = 0;
  damaged->crc32 = crc32(0, reinterpret_cast<uint8_t *>(damaged), sizeof(*damaged));
}

}  // namespace

fitx::result<efi_status> EfiGptBlockDevice::LoadGptEntries(const gpt_header_t &header) {
  entries_.resize(header.entries_count);
  utf8_names_.resize(header.entries_count);

  if (efi_status status = Read(entries_.data(), header.entries * BlockSize(),
                               header.entries_size * header.entries_count);
      status != EFI_SUCCESS) {
    entries_.resize(0);
    utf8_names_.resize(0);

    return fitx::error(status);
  }

  return fitx::ok();
}

fitx::result<efi_status> EfiGptBlockDevice::RestoreFromBackup() {
  gpt_header_t backup;
  if (efi_status status =
          Read(&backup, BlockSize() * block_io_protocol_->Media->LastBlock, sizeof(backup));
      status != EFI_SUCCESS) {
    return fitx::error(status);
  }

  if (!ValidateHeader(backup)) {
    return fitx::error(EFI_NOT_FOUND);
  }

  if (fitx::result res = LoadGptEntries(backup); !res.is_ok()) {
    return res;
  }

  uint32_t entries_crc =
      crc32(0, reinterpret_cast<uint8_t *>(entries_.data()), sizeof(entries_[0]) * entries_.size());
  if (entries_crc != backup.entries_crc) {
    return fitx::error(EFI_NOT_FOUND);
  }

  RestoreHeaderFromGood(backup, &gpt_header_);
  if (efi_status status = Write(&gpt_header_, BlockSize(), sizeof(gpt_header_));
      status != EFI_SUCCESS) {
    return fitx::error(status);
  }

  if (efi_status status = Write(entries_.data(), gpt_header_.entries * BlockSize(),
                                gpt_header_.entries_count * gpt_header_.entries_size);
      status != EFI_SUCCESS) {
    return fitx::error(status);
  }

  return fitx::ok();
}

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
  if (efi_status status = Read(&gpt_header_, BlockSize(), sizeof(gpt_header_));
      status != EFI_SUCCESS) {
    return fitx::error(status);
  }

  // Note: we only read the backup header and entries if the primary is corrupted.
  // This leaves a potential hole where the backup gets silently corrupted
  // and this isn't caught until we need to use it to restore the primary,
  // in which case both headers are corrupted.
  //
  // The alternative would be to always read both headers and
  // potentially restore the backup from the primary.
  // This slows down boot in the common case where everything is fine;
  // it is arguably better to leave this task to a post-boot daemon.
  if (!ValidateHeader(gpt_header_)) {
    auto res = RestoreFromBackup();
    if (!res.is_ok()) {
      return res;
    }
  } else {
    if (auto res = LoadGptEntries(gpt_header_); !res.is_ok()) {
      return res;
    }

    uint32_t entries_crc = crc32(0, reinterpret_cast<uint8_t *>(entries_.data()),
                                 sizeof(entries_[0]) * entries_.size());

    if (entries_crc != gpt_header_.entries_crc) {
      auto res = RestoreFromBackup();
      if (!res.is_ok()) {
        return res;
      }
    }
  }

  // At this point we know we have valid primary and backup header and entries on disk
  // and our in memory copies are synched with data on disk.
  for (size_t i = 0; i < gpt_header_.entries_count; i++) {
    size_t dst_len = utf8_names_[i].size();
    zx_status_t conv_status =
        utf16_to_utf8(reinterpret_cast<const uint16_t *>(entries_[i].name), GPT_NAME_LEN / 2,
                      reinterpret_cast<uint8_t *>(utf8_names_[i].data()), &dst_len);
    if (conv_status != ZX_OK || dst_len > utf8_names_[i].size()) {
      printf("Failed to convert partition name to utf8, %d, %zu\n", conv_status, dst_len);
      return fitx::error(EFI_UNSUPPORTED);
    }
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
  for (size_t i = 0; i < utf8_names_.size(); i++) {
    gpt_entry_t const &entry = entries_[i];
    if (entry.first != 0 && entry.last != 0 && name == utf8_names_[i].data()) {
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
