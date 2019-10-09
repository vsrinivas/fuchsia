// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_JOURNAL_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_JOURNAL_H_

#include <lib/disk-inspector/common-types.h>

#include <fbl/unique_ptr.h>
#include <fs/journal/format.h>
#include <minfs/format.h>

namespace minfs {

// Total number of fields in the on-disk journal structure.
constexpr uint32_t kJournalNumElements = 5;
constexpr char kJournalName[] = "journal";

class JournalObject : public disk_inspector::DiskObject {
 public:
  JournalObject() = delete;
  JournalObject(const JournalObject&) = delete;
  JournalObject(JournalObject&&) = delete;
  JournalObject& operator=(const JournalObject&) = delete;
  JournalObject& operator=(JournalObject&&) = delete;

  explicit JournalObject(std::unique_ptr<fs::JournalInfo> info) : journal_info_(std::move(info)) {}

  // DiskObject interface:
  const char* GetName() const override { return kJournalName; }

  uint32_t GetNumElements() const override { return kJournalNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // Name of DiskObject journal.
  fbl::String name_;

  // Pointer to the minfs journal info.
  std::unique_ptr<fs::JournalInfo> journal_info_;
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_INSPECTOR_JOURNAL_H_
