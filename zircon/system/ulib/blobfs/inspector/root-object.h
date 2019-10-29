// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_INSPECTOR_ROOT_OBJECT_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_INSPECTOR_ROOT_OBJECT_H_

#include <lib/disk-inspector/common-types.h>

#include <blobfs/format.h>

#include "blobfs.h"
#include "inspector-blobfs.h"

namespace blobfs {

// Helper functions to make creating DiskObjects easier from primitive types.
std::unique_ptr<disk_inspector::DiskObjectUint64> CreateUint64DiskObj(fbl::String field_name,
                                                                      const uint64_t* value);

std::unique_ptr<disk_inspector::DiskObjectUint32> CreateUint32DiskObj(fbl::String field_name,
                                                                      const uint32_t* value);

// Total number of elements present in root.
constexpr uint32_t kRootNumElements = 1;
constexpr char kRootName[] = "blobfs-root";

class RootObject : public disk_inspector::DiskObject {
 public:
  RootObject() = delete;
  RootObject(const RootObject&) = delete;
  RootObject(RootObject&&) = delete;
  RootObject& operator=(const RootObject&) = delete;
  RootObject& operator=(RootObject&&) = delete;

  explicit RootObject(std::unique_ptr<InspectorBlobfs> inspector_blobfs)
      : inspector_blobfs_(std::move(inspector_blobfs)) {}

  // DiskObject interface
  const char* GetName() const override { return kRootName; }

  uint32_t GetNumElements() const override { return kRootNumElements; }

  void GetValue(const void** out_buffer, size_t* out_buffer_size) const override;

  std::unique_ptr<DiskObject> GetElementAt(uint32_t index) const override;

 private:
  // Gets the superblock diskObject element at index 0.
  std::unique_ptr<disk_inspector::DiskObject> GetSuperblock() const;

  // Gets the inode_table_ diskObject element at index 1.
  std::unique_ptr<disk_inspector::DiskObject> GetInodeTable() const;

  // Gets the journal diskObject element at index 2.
  std::unique_ptr<disk_inspector::DiskObject> GetJournal() const;

  // Pointer to the Inspector blobfs instance.
  std::unique_ptr<InspectorBlobfs> inspector_blobfs_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_INSPECTOR_ROOT_OBJECT_H_
