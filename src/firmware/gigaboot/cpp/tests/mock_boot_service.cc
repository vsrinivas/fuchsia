// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_boot_service.h"

#include "src/lib/utf_conversion/utf_conversion.h"

efi_loaded_image_protocol* gEfiLoadedImage = nullptr;
efi_system_table* gEfiSystemTable = nullptr;
efi_handle gEfiImageHandle;

namespace gigaboot {

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
  gpt_header_t primary;
  primary.magic = GPT_MAGIC;
  primary.size = GPT_HEADER_SIZE;
  primary.first = kGptFirstUsableBlocks;
  primary.last = total_blocks_ - kGptHeaderBlocks - 1;
  primary.entries = 2;
  primary.entries_count = kGptEntries;
  primary.entries_size = GPT_ENTRY_SIZE;

  uint8_t* start = fake_disk_io_protocol_.contents(0).data();

  // Copy over the primary header. Skip mbr partition.
  memcpy(start + kBlockSize, &primary, sizeof(primary));

  // Initialize partition entries to 0s
  memset(start + 2 * kBlockSize, 0, kGptEntries * sizeof(gpt_entry_t));
}

void BlockDevice::FinalizeGpt() {
  // TODO(b/235489025): Implement backup GPT sync and crc computation once we support full
  // validation and fall back.
}

void BlockDevice::AddGptPartition(const gpt_entry_t& new_entry) {
  ASSERT_GE(new_entry.first, kGptFirstUsableBlocks);
  ASSERT_LE(new_entry.last, total_blocks_ - kGptHeaderBlocks - 1);
  // Search for an empty entry
  uint8_t* start = fake_disk_io_protocol_.contents(0).data() + 2 * kBlockSize;
  for (size_t i = 0; i < kGptEntries; i++) {
    uint8_t* entry_ptr = start + i * sizeof(gpt_entry_t);
    gpt_entry_t current;
    memcpy(&current, entry_ptr, sizeof(gpt_entry_t));
    if (current.first == 0 && current.last == 0) {
      memcpy(entry_ptr, &new_entry, sizeof(gpt_entry_t));
      return;
    }
  }
  ASSERT_TRUE(false);
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
