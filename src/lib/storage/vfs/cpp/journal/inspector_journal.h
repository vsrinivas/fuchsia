// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_JOURNAL_INSPECTOR_JOURNAL_H_
#define SRC_LIB_STORAGE_VFS_CPP_JOURNAL_INSPECTOR_JOURNAL_H_

#include <disk_inspector/common_types.h>

#include "src/lib/storage/vfs/cpp/journal/format.h"

namespace fs {

// Total number of fields in the on-disk journal structure.
constexpr uint32_t kJournalNumElements = 6;
constexpr char kJournalName[] = "journal";
constexpr char kJournalEntriesName[] = "journal-entries";

using BlockReadCallback = std::function<zx_status_t(uint64_t, void*)>;
class JournalObject : public disk_inspector::DiskObject {
 public:
  JournalObject() = delete;
  JournalObject(const JournalObject&) = delete;
  JournalObject(JournalObject&&) = delete;
  JournalObject& operator=(const JournalObject&) = delete;
  JournalObject& operator=(JournalObject&&) = delete;

  JournalObject(fs::JournalInfo info, uint64_t start_block, uint64_t length,
                BlockReadCallback read_block)
      : journal_info_(std::move(info)),
        start_block_(start_block),
        length_(length),
        read_block_(std::move(read_block)) {
    ZX_ASSERT(read_block_ != nullptr);
  }

  // DiskObject interface:
  const char* GetName() const override { return kJournalName; }

  uint32_t GetNumElements() const override { return kJournalNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  fs::JournalInfo journal_info_;
  uint64_t start_block_;
  uint64_t length_;
  BlockReadCallback read_block_;
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_JOURNAL_INSPECTOR_JOURNAL_H_
