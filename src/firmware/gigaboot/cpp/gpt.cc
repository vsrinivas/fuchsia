// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpt.h"

#include <lib/cksum.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <optional>

#include "backends.h"
#include "device_path.h"
#include "partition.h"
#include "src/lib/utf_conversion/utf_conversion.h"
#include "utils.h"

namespace gigaboot {
uint64_t EfiGptBlockDevice::GENERATION_ID = 1;

namespace {
template <typename T>
auto constexpr DivideRoundUp(T t1, T t2) -> decltype(t1 + t2) {
  return (t1 + t2 - 1) / t2;
}

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

gpt_header_t GenerateComplementaryHeader(const gpt_header_t &good) {
  gpt_header_t restored(good);

  restored.backup = good.current;
  restored.current = good.backup;

  // This is an unfortunate hack: for every other entry in the header,
  // it doesn't matter whether the damaged header is the primary or the backup,
  // and similarly for the good header.
  restored.entries = (restored.current == 1) ? 2 : restored.last + 1;

  restored.crc32 = 0;
  restored.crc32 = crc32(0, reinterpret_cast<uint8_t *>(&restored), sizeof(restored));

  return restored;
}

}  // namespace

fit::result<efi_status> EfiGptBlockDevice::LoadGptEntries(const gpt_header_t &header) {
  entries_.resize(header.entries_count);
  utf8_names_.resize(header.entries_count);

  if (efi_status status = Read(entries_.data(), header.entries * BlockSize(),
                               header.entries_size * header.entries_count);
      status != EFI_SUCCESS) {
    entries_.resize(0);
    utf8_names_.resize(0);

    return fit::error(status);
  }

  return fit::ok();
}

fit::result<efi_status> EfiGptBlockDevice::Reinitialize() {
  std::optional<PartitionMap> res =
      PartitionMap::GeneratePartitionMap(GetPartitionCustomizations());
  if (!res) {
    return fit::error(EFI_NOT_FOUND);
  }

  fbl::Vector<PartitionMap::PartitionEntry> const &partitions = res.value().partitions();
  entries_.resize(partitions.size());

  // Block zero is MBR, block one is GPT base, and a max of 128 entries.
  uint64_t const gpt_size = DivideRoundUp((128 * sizeof(gpt_entry_t)), BlockSize());
  uint64_t const base_block = 1 + 1 + gpt_size;
  uint64_t current_block = base_block;
  for (size_t i = 0; i < partitions.size(); i++) {
    gpt_entry_t &entry = entries_[i];
    PartitionMap::PartitionEntry const &partition = partitions[i];

    entry.first = current_block;
    // The 'last' field is inclusive.
    entry.last = entry.first + DivideRoundUp(partition.min_size_bytes, BlockSize()) - 1;
    memcpy(entry.type, partition.type_guid, sizeof(entry.type));
    size_t dst_len = sizeof(entry.name) / 2;
    zx_status_t conv_status =
        utf8_to_utf16(reinterpret_cast<const uint8_t *>(partition.name.data()),
                      partition.name.length(), reinterpret_cast<uint16_t *>(entry.name), &dst_len);
    if (conv_status != ZX_OK || dst_len > sizeof(entry.name)) {
      printf("Failed to convert partition name to utf16, %d, %zu\n", conv_status, dst_len);
    }

    // entry.last is inclusive
    current_block = entry.last + 1;
  }

  // Similar calculation as for base_block: last block is GPT backup header,
  // then 128 entries.
  uint64_t const last_usable_block = LastBlock() - 1 - gpt_size;

  // For real hardware and real backends it is unlikely but not impossible that
  // the partition definitions exceed the size of the disk.
  if (current_block > last_usable_block) {
    return fit::error(EFI_NOT_FOUND);
  }

  // There can be at most one partition that is designated to take all remaining
  // disk space, and if so specified it is required to be the final partition.
  // See the comments for GeneratePartitionMap for more details.
  if (partitions[partitions.size() - 1].min_size_bytes == SIZE_MAX) {
    entries_[entries_.size() - 1].last = last_usable_block;
  }

  gpt_header_ = {
      .magic = GPT_MAGIC,
      .size = GPT_HEADER_SIZE,
      .crc32 = 0,
      .reserved0 = 0,
      .current = 1,
      .backup = LastBlock() - 1,
      .first = base_block,
      .last = last_usable_block,
      .entries = 2,
      .entries_count = static_cast<uint32_t>(entries_.size()),
      .entries_size = GPT_ENTRY_SIZE,
      .entries_crc = crc32(0, reinterpret_cast<uint8_t *>(entries_.data()),
                           entries_.size() * sizeof(gpt_entry_t)),
  };
  gpt_header_.crc32 = crc32(0, reinterpret_cast<uint8_t *>(&gpt_header_), sizeof(gpt_header_));

  // Write everything to disk
  if (efi_status status = Write(&gpt_header_, BlockSize(), sizeof(gpt_header_));
      status != EFI_SUCCESS) {
    return fit::error(status);
  }

  if (efi_status status = Write(entries_.data(), BlockSize() * gpt_header_.entries,
                                sizeof(entries_[0]) * entries_.size());
      status != EFI_SUCCESS) {
    return fit::error(status);
  }

  gpt_header_t backup = GenerateComplementaryHeader(gpt_header_);
  if (efi_status status = Write(&backup, BlockSize() * LastBlock(), sizeof(backup));
      status != EFI_SUCCESS) {
    return fit::error(status);
  }

  if (efi_status status = Write(entries_.data(), BlockSize() * backup.entries,
                                sizeof(entries_[0]) * entries_.size());
      status != EFI_SUCCESS) {
    return fit::error(status);
  }

  // Synch our own data with newly written data
  utf8_names_.reset();
  utf8_names_.resize(partitions.size());
  for (size_t i = 0; i < partitions.size(); i++) {
    memcpy(&utf8_names_[i], partitions[i].name.data(), partitions[i].name.size());
  }

  // Wait until the end to update the generation id
  generation_id_ = ++GENERATION_ID;
  return fit::ok();
}

fit::result<efi_status> EfiGptBlockDevice::RestoreFromBackup() {
  gpt_header_t backup;
  if (efi_status status = Read(&backup, BlockSize() * LastBlock(), sizeof(backup));
      status != EFI_SUCCESS) {
    return fit::error(status);
  }

  if (!ValidateHeader(backup)) {
    return fit::error(EFI_NOT_FOUND);
  }

  if (fit::result res = LoadGptEntries(backup); !res.is_ok()) {
    return res;
  }

  uint32_t entries_crc =
      crc32(0, reinterpret_cast<uint8_t *>(entries_.data()), sizeof(entries_[0]) * entries_.size());
  if (entries_crc != backup.entries_crc) {
    return fit::error(EFI_NOT_FOUND);
  }

  gpt_header_ = GenerateComplementaryHeader(backup);
  if (efi_status status = Write(&gpt_header_, BlockSize(), sizeof(gpt_header_));
      status != EFI_SUCCESS) {
    return fit::error(status);
  }

  if (efi_status status = Write(entries_.data(), gpt_header_.entries * BlockSize(),
                                gpt_header_.entries_count * gpt_header_.entries_size);
      status != EFI_SUCCESS) {
    return fit::error(status);
  }

  return fit::ok();
}

fit::result<efi_status, EfiGptBlockDevice> EfiGptBlockDevice::Create(efi_handle device_handle) {
  EfiGptBlockDevice ret;
  // Open the block IO protocol for this device.
  auto block_io = EfiOpenProtocol<efi_block_io_protocol>(device_handle);
  if (block_io.is_error()) {
    printf("Failed to open block io protocol %s\n", EfiStatusToString(block_io.error_value()));
    return fit::error(block_io.error_value());
  }
  ret.block_io_protocol_ = std::move(block_io.value());

  // Open the disk IO protocol for this device.
  auto disk_io = EfiOpenProtocol<efi_disk_io_protocol>(device_handle);
  if (disk_io.is_error()) {
    printf("Failed to open disk io protocol %s\n", EfiStatusToString(disk_io.error_value()));
    return fit::error(disk_io.error_value());
  }
  ret.disk_io_protocol_ = std::move(disk_io.value());

  return fit::ok(std::move(ret));
}

fit::result<efi_status> EfiGptBlockDevice::Load() {
  // First block is MBR. Read the second block for the GPT header.
  if (efi_status status = Read(&gpt_header_, BlockSize(), sizeof(gpt_header_));
      status != EFI_SUCCESS) {
    return fit::error(status);
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
      return fit::error(EFI_UNSUPPORTED);
    }
  }

  // Wait until the end to update the generation id
  // so that it never spuriously matches.
  generation_id_ = GENERATION_ID;
  return fit::ok();
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
  if (generation_id_ != GENERATION_ID) {
    if (!Load().is_ok()) {
      return nullptr;
    }
  }

  for (size_t i = 0; i < utf8_names_.size(); i++) {
    gpt_entry_t const &entry = entries_[i];
    if (entry.first != 0 && entry.last != 0 && name == utf8_names_[i].data()) {
      return &entry;
    }
  }

  return nullptr;
}

fit::result<efi_status, size_t> EfiGptBlockDevice::CheckAndGetPartitionAccessRangeInStorage(
    std::string_view name, size_t offset, size_t length) {
  const gpt_entry_t *entry = FindPartition(name);
  if (!entry) {
    return fit::error(EFI_NOT_FOUND);
  }

  size_t block_size = BlockSize();
  size_t abs_offset = entry->first * block_size + offset;
  if (abs_offset + length > (entry->last + 1) * block_size) {
    return fit::error(EFI_INVALID_PARAMETER);
  }

  return fit::ok(abs_offset);
}

fit::result<efi_status> EfiGptBlockDevice::ReadPartition(std::string_view name, size_t offset,
                                                         size_t length, void *out) {
  auto res = CheckAndGetPartitionAccessRangeInStorage(name, offset, length);
  if (res.is_error()) {
    printf("ReadPartition: failed while checking and getting read range %s\n",
           EfiStatusToString(res.error_value()));
    return fit::error(res.error_value());
  }

  efi_status status = Read(out, res.value(), length);
  if (status != EFI_SUCCESS) {
    return fit::error(status);
  }

  return fit::ok();
}

fit::result<efi_status> EfiGptBlockDevice::WritePartition(std::string_view name, const void *data,
                                                          size_t offset, size_t length) {
  auto res = CheckAndGetPartitionAccessRangeInStorage(name, offset, length);
  if (res.is_error()) {
    printf("WritePartition: failed while checking and getting write range %s\n",
           EfiStatusToString(res.error_value()));
    return fit::error(res.error_value());
  }

  efi_status status = Write(data, res.value(), length);
  if (status != EFI_SUCCESS) {
    return fit::error(status);
  }

  return fit::ok();
}

cpp20::span<std::array<char, GPT_NAME_LEN / 2>> EfiGptBlockDevice::ListPartitionNames() {
  return cpp20::span(utf8_names_.begin(), utf8_names_.end());
}

// TODO(https://fxbug.dev/79197): The function currently only finds the storage devie that hosts
// the currently running image. This can be a problem when booting from USB. Add support to handle
// the USB case.
fit::result<efi_status, EfiGptBlockDevice> FindEfiGptDevice() {
  auto image_device_path = EfiOpenProtocol<efi_device_path_protocol>(gEfiLoadedImage->DeviceHandle);
  if (image_device_path.is_error()) {
    printf("Failed to open device path protocol %s\n",
           EfiStatusToString(image_device_path.error_value()));
    return fit::error{image_device_path.error_value()};
  }

  // Find all handles that support block io protocols.
  auto block_io_supported_handles = EfiLocateHandleBufferByProtocol<efi_block_io_protocol>();
  if (block_io_supported_handles.is_error()) {
    printf("Failed to locate handles supporting block io protocol\n");
    return fit::error(block_io_supported_handles.error_value());
  }

  // Scan all handles and find the one from which the currently running image comes.
  // This is done by checking if they share common device path prefix.
  for (auto handle : block_io_supported_handles->AsSpan()) {
    auto block_io = EfiOpenProtocol<efi_block_io_protocol>(handle);
    if (block_io.is_error()) {
      printf("Failed to open block io protocol\n");
      return fit::error(block_io.error_value());
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
      return fit::error(device_path.error_value());
    }

    if (EfiDevicePathNode::StartsWith(image_device_path.value().get(), device_path.value().get())) {
      // Open the disk io protocol
      auto efi_gpt_device = EfiGptBlockDevice::Create(handle);
      if (efi_gpt_device.is_error()) {
        printf("Failed to create GPT device\n");
        return fit::error(efi_gpt_device.error_value());
      }

      return fit::ok(std::move(efi_gpt_device.value()));
    }
  }

  printf("No matching block device found\n");
  return fit::error{EFI_NOT_FOUND};
}

}  // namespace gigaboot
