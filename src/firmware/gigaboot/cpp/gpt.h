// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_GPT_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_GPT_H_

#include <lib/fit/result.h>
#include <zircon/hw/gpt.h>

#include <array>
#include <optional>
#include <string_view>

#include <fbl/vector.h>

#include "utils.h"

namespace gigaboot {

class EfiGptBlockDevice {
 public:
  static fit::result<efi_status, EfiGptBlockDevice> Create(efi_handle device_handle);

  // No copy.
  EfiGptBlockDevice(const EfiGptBlockDevice &) = delete;
  EfiGptBlockDevice &operator=(const EfiGptBlockDevice &) = delete;

  EfiGptBlockDevice(EfiGptBlockDevice &&) = default;
  EfiGptBlockDevice &operator=(EfiGptBlockDevice &&) = default;

  fit::result<efi_status> ReadPartition(std::string_view name, size_t offset, size_t length,
                                        void *out);
  fit::result<efi_status> WritePartition(std::string_view name, const void *data, size_t offset,
                                         size_t length);

  gpt_header_t const &GptHeader() const { return gpt_header_; }
  cpp20::span<std::array<char, GPT_NAME_LEN / 2>> ListPartitionNames();

  size_t BlockSize() { return block_io_protocol_->Media->BlockSize; }
  uint64_t LastBlock() { return block_io_protocol_->Media->LastBlock; }

  // Find partition info.
  //
  // Note: this function will reload the GPT if the on-disk GPT has been reinitialized
  // by another EfiGptBlockDevice that references the same physical device.
  const gpt_entry_t *FindPartition(std::string_view name);

  // Load GPT from device.
  //
  // Note: this function MAY reset the primary GPT but NOT the backup.
  // The backup is only read and verified if the primary is corrupted;
  // if this is the case the primary is restored from the backup.
  //
  // There is a hole where the backup is corrupted first.
  // At some point, if the primary is ever corrupted, the load will fail.
  // To prevent this we would need to read both tables all the time during boot
  // and then repair any damage done to either table.
  // Reading both tables all the time slows down boot in the common case where
  // both tables are fine. This sort of verification and repair is arguably better
  // suited to a post-boot daemon.
  fit::result<efi_status> Load();

  // Reinitialize device's GPT.
  //
  // Generates the factory default partition table, writes it to disk,
  // and updates internal data structures.
  // Return values from FindPartition are invalidated.
  // Return values from ListPartitionNames are invalidated.
  //
  // Note: this function requires all other EfiGptBlockDevice objects
  // that reference the same disk to reread the disk's partition information.
  // Subsequent method calls on those objects may result in reloading
  // the partition information from disk.
  fit::result<efi_status> Reinitialize();

 private:
  static uint64_t GENERATION_ID;
  uint64_t generation_id_;

  // The parameters we need for reading/writing partitions live in both block and disk io protocols.
  EfiProtocolPtr<efi_block_io_protocol> block_io_protocol_;
  EfiProtocolPtr<efi_disk_io_protocol> disk_io_protocol_;

  gpt_header_t gpt_header_;

  // These two vectors are tied together:
  // utf8_names_[i] is the name for entries_[i].
  // The reason that they are two separate vectors is that it's
  // much easier to read the entries straight off the disk and
  // into a vector in a single operation,
  // and it's also easier to calculate the entries' crc on the raw bytes
  // as a single, contiguous chunk.
  fbl::Vector<gpt_entry_t> entries_;
  fbl::Vector<std::array<char, GPT_NAME_LEN / 2>> utf8_names_;

  EfiGptBlockDevice() : generation_id_(GENERATION_ID - 1) {}
  efi_status Read(void *buffer, size_t offset, size_t length);
  efi_status Write(const void *data, size_t offset, size_t length);

  fit::result<efi_status> LoadGptEntries(const gpt_header_t &);
  fit::result<efi_status> RestoreFromBackup();

  // Check that the given range is within boundary of a partition and returns the absolute offset
  // relative to the storage start.
  fit::result<efi_status, size_t> CheckAndGetPartitionAccessRangeInStorage(std::string_view name,
                                                                           size_t offset,
                                                                           size_t length);
};

fit::result<efi_status, EfiGptBlockDevice> FindEfiGptDevice();

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_GPT_H_
