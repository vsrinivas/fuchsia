// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file includes necessary methods for inspecting various on-disk structures
// of a MinFS filesystem.

#ifndef SRC_STORAGE_MINFS_INSPECTOR_INSPECTOR_PRIVATE_H_
#define SRC_STORAGE_MINFS_INSPECTOR_INSPECTOR_PRIVATE_H_

#include <sys/stat.h>

#include <disk_inspector/common_types.h>
#include <fbl/unique_fd.h>

#include "src/lib/storage/vfs/cpp/journal/format.h"
#include "src/storage/minfs/allocator/inode_manager.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/runner.h"

namespace minfs {

// Total number of elements present in root.
constexpr uint32_t kRootNumElements = 4;
constexpr char kRootName[] = "minfs-root";

class RootObject : public disk_inspector::DiskObject {
 public:
  RootObject() = delete;
  RootObject(const RootObject&) = delete;
  RootObject(RootObject&&) = delete;
  RootObject& operator=(const RootObject&) = delete;
  RootObject& operator=(RootObject&&) = delete;

  explicit RootObject(std::unique_ptr<Runner> fs) : runner_(std::move(fs)), fs_(runner_->minfs()) {}

  // DiskObject interface
  const char* GetName() const override { return kRootName; }

  uint32_t GetNumElements() const override { return kRootNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // Gets the superblock diskObject element at index 0.
  std::unique_ptr<disk_inspector::DiskObject> GetSuperBlock() const;

  // Gets the inode_table_ diskObject element at index 1.
  std::unique_ptr<disk_inspector::DiskObject> GetInodeTable() const;

  // Gets the journal diskObject element at index 2.
  std::unique_ptr<disk_inspector::DiskObject> GetJournal() const;

  // Gets the journal diskObject element at index 3.
  std::unique_ptr<disk_inspector::DiskObject> GetBackupSuperBlock() const;

  // Pointer to the Minfs instance.
  std::unique_ptr<Runner> runner_;
  Minfs& fs_;
};

std::unique_ptr<disk_inspector::DiskObjectUint64> CreateUint64DiskObj(fbl::String fieldName,
                                                                      const uint64_t* value);

std::unique_ptr<disk_inspector::DiskObjectUint32> CreateUint32DiskObj(fbl::String fieldName,
                                                                      const uint32_t* value);

std::unique_ptr<disk_inspector::DiskObjectUint64Array> CreateUint64ArrayDiskObj(
    fbl::String fieldName, const uint64_t* value, size_t size);

std::unique_ptr<disk_inspector::DiskObjectUint32Array> CreateUint32ArrayDiskObj(
    fbl::String fieldName, const uint32_t* value, size_t size);

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_INSPECTOR_INSPECTOR_PRIVATE_H_
