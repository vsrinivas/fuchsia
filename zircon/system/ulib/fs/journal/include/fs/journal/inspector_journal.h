// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_JOURNAL_INSPECTOR_JOURNAL_H_
#define FS_JOURNAL_INSPECTOR_JOURNAL_H_

#include <disk_inspector/common_types.h>
#include <fs/inspectable.h>
#include <fs/journal/format.h>

namespace fs {

// Total number of fields in the on-disk journal structure.
constexpr uint32_t kJournalNumElements = 6;
constexpr char kJournalName[] = "journal";
constexpr char kJournalEntriesName[] = "journal-entries";

class JournalObject : public disk_inspector::DiskObject {
 public:
  JournalObject() = delete;
  JournalObject(const JournalObject&) = delete;
  JournalObject(JournalObject&&) = delete;
  JournalObject& operator=(const JournalObject&) = delete;
  JournalObject& operator=(JournalObject&&) = delete;

  JournalObject(fs::JournalInfo info, uint64_t start_block, uint64_t length,
                const Inspectable* inspectable)
      : journal_info_(std::move(info)),
        start_block_(start_block),
        length_(length),
        inspectable_(inspectable) {}

  // DiskObject interface:
  const char* GetName() const override { return kJournalName; }

  uint32_t GetNumElements() const override { return kJournalNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  fs::JournalInfo journal_info_;
  uint64_t start_block_;
  uint64_t length_;
  const Inspectable* inspectable_;
};

}  // namespace fs

#endif  // FS_JOURNAL_INSPECTOR_JOURNAL_H_
