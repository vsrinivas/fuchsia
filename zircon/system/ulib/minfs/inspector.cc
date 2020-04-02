// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/stat.h>

#include <block-client/cpp/block-device.h>
#include <disk_inspector/common_types.h>
#include <fbl/unique_fd.h>
#include <fs/journal/inspector_journal.h>
#include <minfs/bcache.h>
#include <minfs/inspector.h>

#include "inspector_inode_table.h"
#include "inspector_private.h"
#include "inspector_superblock.h"

namespace minfs {

std::unique_ptr<disk_inspector::DiskObjectUint64> CreateUint64DiskObj(fbl::String fieldName,
                                                                      const uint64_t* value) {
  return std::make_unique<disk_inspector::DiskObjectUint64>(fieldName, value);
}

std::unique_ptr<disk_inspector::DiskObjectUint32> CreateUint32DiskObj(fbl::String fieldName,
                                                                      const uint32_t* value) {
  return std::make_unique<disk_inspector::DiskObjectUint32>(fieldName, value);
}

std::unique_ptr<disk_inspector::DiskObjectUint64Array> CreateUint64ArrayDiskObj(
    fbl::String fieldName, const uint64_t* value, size_t size) {
  return std::make_unique<disk_inspector::DiskObjectUint64Array>(fieldName, value, size);
}

std::unique_ptr<disk_inspector::DiskObjectUint32Array> CreateUint32ArrayDiskObj(
    fbl::String fieldName, const uint32_t* value, size_t size) {
  return std::make_unique<disk_inspector::DiskObjectUint32Array>(fieldName, value, size);
}

zx_status_t Inspector::GetRoot(std::unique_ptr<disk_inspector::DiskObject>* out) {
  std::unique_ptr<minfs::Bcache> bc;
  bool readonly_device = false;
  zx_status_t status = CreateBcache(std::move(device_), &readonly_device, &bc);
  if (status != ZX_OK) {
    fprintf(stderr, "minfsInspector: cannot create block cache\n");
    return status;
  }

  status = CreateRoot(std::move(bc), out);
  if (status != ZX_OK) {
    fprintf(stderr, "minfsInspector: cannot create root object\n");
    return status;
  }
  return ZX_OK;
}

zx_status_t Inspector::CreateRoot(std::unique_ptr<Bcache> bc,
                                  std::unique_ptr<disk_inspector::DiskObject>* out) {
  MountOptions options = {};
  options.readonly_after_initialization = true;
  options.repair_filesystem = false;
  options.use_journal = false;
  std::unique_ptr<Minfs> fs;
  zx_status_t status = Minfs::Create(std::move(bc), options, &fs);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfsInspector: Create Failed to Create Minfs: %d\n", status);
    return status;
  }
  *out = std::unique_ptr<disk_inspector::DiskObject>(new RootObject(std::move(fs)));
  return ZX_OK;
}

std::unique_ptr<disk_inspector::DiskObject> RootObject::GetSuperBlock() const {
  return std::unique_ptr<disk_inspector::DiskObject>(
      new SuperBlockObject(fs_->Info(), SuperblockType::kPrimary));
}

std::unique_ptr<disk_inspector::DiskObject> RootObject::GetInodeTable() const {
  return std::unique_ptr<disk_inspector::DiskObject>(new InodeTableObject(
      fs_->GetInodeManager(), fs_->Info().alloc_inode_count, fs_->Info().inode_count));
}

std::unique_ptr<disk_inspector::DiskObject> RootObject::GetJournal() const {
  char data[kMinfsBlockSize];

  const Superblock& superblock = fs_->Info();
  uint64_t start_block = JournalStartBlock(superblock);
  uint64_t length = JournalBlocks(superblock);
  if (fs_->ReadBlock(static_cast<blk_t>(start_block), data) < 0) {
    FS_TRACE_ERROR("minfsInspector: could not read journal block\n");
    return nullptr;
  }
  fs::JournalInfo* info = reinterpret_cast<fs::JournalInfo*>(data);
  return std::unique_ptr<disk_inspector::DiskObject>(
      new fs::JournalObject(*info, start_block, length, fs_.get()));
}

std::unique_ptr<disk_inspector::DiskObject> RootObject::GetBackupSuperBlock() const {
  char data[kMinfsBlockSize];
  const Superblock& info = fs_->Info();

  uint64_t location =
      ((info.flags & kMinfsFlagFVM) == 0) ? kNonFvmSuperblockBackup : kFvmSuperblockBackup;
  if (fs_->ReadBlock(static_cast<blk_t>(location), &data) < 0) {
    FS_TRACE_ERROR("minfsInspector: could not read backup superblock\n");
    return nullptr;
  }
  auto backup_info = reinterpret_cast<Superblock&>(data);
  return std::unique_ptr<disk_inspector::DiskObject>(
      new SuperBlockObject(backup_info, SuperblockType::kBackup));
}

void RootObject::GetValue(const void** out_buffer, size_t* out_buffer_size) const {
  ZX_DEBUG_ASSERT_MSG(false, "Invalid GetValue call for non primitive data type.");
}

std::unique_ptr<disk_inspector::DiskObject> RootObject::GetElementAt(uint32_t index) const {
  switch (index) {
    case 0: {
      // Super Block
      return GetSuperBlock();
    }
    case 1: {
      // Inode Table
      return GetInodeTable();
    }
    case 2: {
      // Journal
      return GetJournal();
    }
    case 3: {
      // Backup Superblock
      return GetBackupSuperBlock();
    }
  };
  return nullptr;
}

}  // namespace minfs
