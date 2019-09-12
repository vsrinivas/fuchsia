// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs_fixtures.h"

#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fvm/format.h>
#include <lib/fzl/fdio.h>
#include <zxtest/zxtest.h>

namespace {

bool GetFsInfo(fuchsia_io_FilesystemInfo* info) {
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  if (!fd) {
    return false;
  }

  zx_status_t status;
  fzl::FdioCaller caller(std::move(fd));
  zx_status_t io_status =
      fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, info);
  if (io_status != ZX_OK) {
    status = io_status;
  }

  if (status != ZX_OK) {
    printf("Could not query block FS info: %s\n", zx_status_get_string(status));
    return false;
  }
  return true;
}

void CheckBlobfsInfo() {
  fuchsia_io_FilesystemInfo info;
  ASSERT_TRUE(GetFsInfo(&info));

  const char kFsName[] = "blobfs";
  const char* name = reinterpret_cast<const char*>(info.name);
  ASSERT_STR_EQ(kFsName, name);
  ASSERT_LE(info.used_nodes, info.total_nodes, "Used nodes greater than free nodes");
  ASSERT_LE(info.used_bytes, info.total_bytes, "Used bytes greater than free bytes");
}

}  // namespace

void BlobfsTest::CheckInfo() {
  CheckBlobfsInfo();
}

void BlobfsTestWithFvm::CheckInfo() {
  CheckBlobfsInfo();
}

void BlobfsTestWithFvm::CheckPartitionSize() {
  // Minimum size required by ResizePartition test:
  const size_t kMinDataSize = 507 * kTestFvmSliceSize;
  const size_t kMinFvmSize =
      fvm::MetadataSize(kMinDataSize, kTestFvmSliceSize) * 2 + kMinDataSize;  // ~8.5mb
  ASSERT_GE(environment_->disk_size(), kMinFvmSize, "Insufficient disk space for FVM tests");
}

void MakeBlob(const fs_test_utils::BlobInfo* info, fbl::unique_fd* fd) {
  fd->reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(*fd, "Failed to create blob");
  ASSERT_EQ(ftruncate(fd->get(), info->size_data), 0);
  ASSERT_EQ(fs_test_utils::StreamAll(write, fd->get(), info->data.get(), info->size_data), 0,
            "Failed to write Data");
  ASSERT_TRUE(fs_test_utils::VerifyContents(fd->get(), info->data.get(), info->size_data));
}
