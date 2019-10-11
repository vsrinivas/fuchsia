// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_JOURNAL_ENTRIES_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_JOURNAL_ENTRIES_H_

#include <lib/disk-inspector/common-types.h>

#include <fbl/string_printf.h>
#include <fs/journal/format.h>
#include <minfs/format.h>

#include "minfs-private.h"

namespace minfs {

constexpr char kJournalEntriesName[] = "journal-entries";

class JournalBlock : public disk_inspector::DiskObject {
 public:
  JournalBlock() = delete;
  JournalBlock(const JournalBlock&) = delete;
  JournalBlock(JournalBlock&&) = delete;
  JournalBlock& operator=(const JournalBlock&) = delete;
  JournalBlock& operator=(JournalBlock&&) = delete;

  JournalBlock(uint32_t index, fs::JournalInfo info, std::array<uint8_t, kMinfsBlockSize> block);

  // DiskObject interface:
  const char* GetName() const override { return name_.c_str(); }

  uint32_t GetNumElements() const override;

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  const uint32_t index_ = 0;
  const fs::JournalInfo journal_info_;
  const std::array<uint8_t, kMinfsBlockSize> block_;
  fbl::String name_;
  fs::JournalObjectType object_type_;
  uint32_t num_elements_ = 0;
};

class JournalEntries : public disk_inspector::DiskObject {
 public:
  JournalEntries() = delete;
  JournalEntries(const JournalEntries&) = delete;
  JournalEntries(JournalEntries&&) = delete;
  JournalEntries& operator=(const JournalEntries&) = delete;
  JournalEntries& operator=(JournalEntries&&) = delete;

  JournalEntries(fs::JournalInfo info, uint64_t start_block, uint64_t length,
                 const InspectableFilesystem* fs)
      : journal_info_(std::move(info)), start_block_(start_block), length_(length), fs_(fs) {}

  // DiskObject interface:
  const char* GetName() const override { return kJournalEntriesName; }

  uint32_t GetNumElements() const override { return static_cast<uint32_t>(length_); }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  fs::JournalInfo journal_info_;
  uint64_t start_block_;
  uint64_t length_;
  const InspectableFilesystem* fs_;
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_JOURNAL_ENTRIES_H_
