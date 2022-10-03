// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_boot_service.h"

#include <lib/cksum.h>
#include <lib/stdcompat/span.h>

#include "src/lib/utf_conversion/utf_conversion.h"

efi_loaded_image_protocol* gEfiLoadedImage = nullptr;
efi_system_table* gEfiSystemTable = nullptr;
efi_handle gEfiImageHandle;

namespace gigaboot {

namespace {

void RecalculateGptCrcs(cpp20::span<const gpt_entry_t> entries, gpt_header_t* header) {
  header->entries_crc =
      crc32(0, reinterpret_cast<uint8_t const*>(entries.data()), entries.size_bytes());

  header->crc32 = 0;
  header->crc32 = crc32(0, reinterpret_cast<uint8_t*>(header), sizeof(*header));
}

}  // namespace

// A helper that creates a realistic device path protocol
void Device::InitDevicePathProtocol(std::vector<std::string_view> path_nodes) {
  // UEFI specification chapter 10. `efi_device_path_protocol*` is an array of
  // variable length struct. Specifically, each element is
  // `efi_device_path_protocol struct` + path data.
  device_path_buffer_.clear();
  for (auto name : path_nodes) {
    uint16_t node_size = static_cast<uint16_t>(name.size()) + 4;
    device_path_buffer_.push_back(DEVICE_PATH_HARDWARE);
    device_path_buffer_.push_back(0);
    device_path_buffer_.push_back(node_size & 0xFF);
    device_path_buffer_.push_back(node_size >> 8);
    device_path_buffer_.insert(device_path_buffer_.end(),
                               reinterpret_cast<const uint8_t*>(name.data()),
                               reinterpret_cast<const uint8_t*>(name.data() + name.size()));
  }
  device_path_buffer_.push_back(DEVICE_PATH_END);
  device_path_buffer_.push_back(0);
  device_path_buffer_.push_back(4);
  device_path_buffer_.push_back(0);
}

BlockDevice::BlockDevice(std::vector<std::string_view> paths, size_t blocks)
    : Device(paths), total_blocks_(blocks) {
  memset(&block_io_media_, 0, sizeof(block_io_media_));
  block_io_media_.BlockSize = kBlockSize;
  block_io_media_.LastBlock = blocks - 1;
  block_io_media_.MediaPresent = true;
  block_io_protocol_ = {
      .Revision = 0,  // don't care
      .Media = &this->block_io_media_,
      .Reset = nullptr,        // don't care
      .ReadBlocks = nullptr,   // don't care
      .WriteBlocks = nullptr,  // don't care
      .FlushBlocks = nullptr,  // don't care
  };
  // Only support MediaId = 0. Allocate buffer to serve as block storage.
  fake_disk_io_protocol_.contents(/*MediaId=*/0) = std::vector<uint8_t>(blocks * kBlockSize);
}

void BlockDevice::InitializeGpt() {
  ASSERT_GT(total_blocks_, 2 * kGptHeaderBlocks + 1);
  // Entries start on a block
  uint8_t* start = fake_disk_io_protocol_.contents(0).data();

  gpt_header_t primary = {
      .magic = GPT_MAGIC,
      .size = GPT_HEADER_SIZE,
      .crc32 = 0,
      .reserved0 = 0,
      .current = 1,
      .backup = total_blocks_ - 1,
      .first = kGptFirstUsableBlocks,
      .last = total_blocks_ - kGptHeaderBlocks - 1,
      .entries = 2,
      .entries_count = kGptEntries,
      .entries_size = GPT_ENTRY_SIZE,
      .entries_crc = 0,
  };

  gpt_header_t backup = {
      .magic = GPT_MAGIC,
      .size = GPT_HEADER_SIZE,
      .crc32 = 0,
      .reserved0 = 0,
      .current = total_blocks_ - 1,
      .backup = 1,
      .first = kGptFirstUsableBlocks,
      .last = total_blocks_ - kGptHeaderBlocks - 1,
      .entries = total_blocks_ - kGptHeaderBlocks,
      .entries_count = kGptEntries,
      .entries_size = GPT_ENTRY_SIZE,
      .entries_crc = 0,
  };

  primary.entries_crc =
      crc32(0, start + primary.entries * kBlockSize, primary.entries_size * primary.entries_count);
  backup.entries_crc = primary.entries_crc;

  primary.crc32 = crc32(0, reinterpret_cast<uint8_t*>(&primary), sizeof(primary));
  backup.crc32 = crc32(0, reinterpret_cast<uint8_t*>(&backup), sizeof(backup));

  // Copy over the primary header. Skip mbr partition.
  memcpy(start + kBlockSize, &primary, sizeof(primary));

  // Copy the backup header.
  memcpy(start + (backup.current * kBlockSize), &backup, sizeof(backup));

  // Initialize partition entries to 0s
  memset(start + primary.entries * kBlockSize, 0, kGptEntries * sizeof(gpt_entry_t));
}

void BlockDevice::AddGptPartition(const gpt_entry_t& new_entry) {
  ASSERT_GE(new_entry.first, kGptFirstUsableBlocks);
  ASSERT_LE(new_entry.last, total_blocks_ - kGptHeaderBlocks - 1);

  uint8_t* const data = fake_disk_io_protocol_.contents(0).data();

  gpt_header_t* primary_header = reinterpret_cast<gpt_header_t*>(data + kBlockSize);
  gpt_header_t* backup_header =
      reinterpret_cast<gpt_header_t*>(data + (total_blocks_ - 1) * kBlockSize);
  gpt_entry_t* primary_entries = reinterpret_cast<gpt_entry_t*>(data + 2 * kBlockSize);
  gpt_entry_t* backup_entries =
      reinterpret_cast<gpt_entry_t*>(data + (total_blocks_ - kGptHeaderBlocks) * kBlockSize);
  cpp20::span<const gpt_entry_t> entries_span(primary_entries, primary_header->entries_count);

  // Search for an empty entry
  for (size_t i = 0; i < kGptEntries; i++) {
    if (primary_entries[i].first == 0 && primary_entries[i].last == 0) {
      ASSERT_EQ(backup_entries[i].first, 0UL);
      ASSERT_EQ(backup_entries[i].last, 0UL);

      memcpy(&primary_entries[i], &new_entry, sizeof(new_entry));
      memcpy(&backup_entries[i], &new_entry, sizeof(new_entry));

      RecalculateGptCrcs(entries_span, primary_header);
      RecalculateGptCrcs(entries_span, backup_header);

      return;
    }
  }
  ASSERT_TRUE(false);
}

Tcg2Device::Tcg2Device() : Device({}) {
  memset(&tcg2_protocol_, 0, sizeof(tcg2_protocol_));
  tcg2_protocol_.protocol_.GetCapability = Tcg2Device::GetCapability;
  tcg2_protocol_.protocol_.SubmitCommand = Tcg2Device::SubmitCommand;
}

efi_status Tcg2Device::GetCapability(struct efi_tcg2_protocol*,
                                     efi_tcg2_boot_service_capability* out) {
  *out = {};
  return EFI_SUCCESS;
}

efi_status Tcg2Device::SubmitCommand(struct efi_tcg2_protocol* protocol, uint32_t block_size,
                                     uint8_t* block_data, uint32_t output_size,
                                     uint8_t* output_data) {
  Tcg2Device::Protocol* protocol_data_ = reinterpret_cast<Tcg2Device::Protocol*>(protocol);
  protocol_data_->last_command_ = std::vector<uint8_t>(block_data, block_data + block_size);
  return EFI_SUCCESS;
}

efi_status MockStubService::LocateProtocol(const efi_guid* protocol, void* registration,
                                           void** intf) {
  if (IsProtocol<efi_tcg2_protocol>(*protocol)) {
    for (auto& ele : devices_) {
      if (auto protocol = ele->GetTcg2Protocol(); protocol) {
        *intf = protocol;
        return EFI_SUCCESS;
      }
    }
  }

  return EFI_UNSUPPORTED;
}

efi_status MockStubService::LocateHandleBuffer(efi_locate_search_type search_type,
                                               const efi_guid* protocol, void* search_key,
                                               size_t* num_handles, efi_handle** buf) {
  // We'll only ever use ByProtocol search type.
  if (search_type != ByProtocol) {
    return EFI_UNSUPPORTED;
  }

  if (IsProtocol<efi_block_io_protocol>(*protocol)) {
    // Find all handles that support block io protocols.
    std::vector<efi_handle> lists;
    for (auto& ele : devices_) {
      if (ele->GetBlockIoProtocol()) {
        lists.push_back(ele);
      }
    }

    // The returned list to store in `buf` is expected to be freed with BootServices->FreePool()
    // eventually. Thus here we allocate the buffer with BootServices->AllocatePool() and copy over
    // the result.
    *num_handles = lists.size();
    size_t size_in_bytes = lists.size() * sizeof(efi_handle);
    void* buffer;
    efi_status status = gEfiSystemTable->BootServices->AllocatePool(EfiLoaderData /*don't care*/,
                                                                    size_in_bytes, &buffer);
    if (status != EFI_SUCCESS) {
      return status;
    }

    memcpy(buffer, lists.data(), size_in_bytes);
    *buf = reinterpret_cast<efi_handle*>(buffer);
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

efi_status MockStubService::OpenProtocol(efi_handle handle, const efi_guid* protocol, void** intf,
                                         efi_handle agent_handle, efi_handle controller_handle,
                                         uint32_t attributes) {
  // The given handle must be a pointer to one of the registered devices added to `devices_`.
  auto iter_find = std::find(devices_.begin(), devices_.end(), handle);
  if (iter_find == devices_.end()) {
    return EFI_NOT_FOUND;
  }

  if (IsProtocol<efi_device_path_protocol>(*protocol)) {
    *intf = (*iter_find)->GetDevicePathProtocol();
  } else if (IsProtocol<efi_block_io_protocol>(*protocol)) {
    *intf = (*iter_find)->GetBlockIoProtocol();
  } else if (IsProtocol<efi_disk_io_protocol>(*protocol)) {
    *intf = (*iter_find)->GetDiskIoProtocol();
  }

  return *intf ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

efi_status MockStubService::GetMemoryMap(size_t* memory_map_size, efi_memory_descriptor* memory_map,
                                         size_t* map_key, size_t* desc_size,
                                         uint32_t* desc_version) {
  *map_key = 0;
  *desc_version = 0;
  *desc_size = sizeof(efi_memory_descriptor);
  size_t total_size = memory_map_.size() * sizeof(efi_memory_descriptor);
  if (*memory_map_size < total_size) {
    return EFI_INVALID_PARAMETER;
  }

  *memory_map_size = total_size;
  memcpy(memory_map, memory_map_.data(), total_size);

  return EFI_SUCCESS;
}

void SetGptEntryName(const char* name, gpt_entry_t& entry) {
  size_t dst_len = sizeof(entry.name) / sizeof(uint16_t);
  utf8_to_utf16(reinterpret_cast<const uint8_t*>(name), strlen(name),
                reinterpret_cast<uint16_t*>(entry.name), &dst_len);
}

}  // namespace gigaboot
