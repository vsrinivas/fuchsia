// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs_fixtures.h"

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>

#include <fvm/format.h>
#include <zxtest/zxtest.h>

namespace {

void CheckBlobfsInfo(fs::FilesystemTest* test) {
  ::llcpp::fuchsia::io::FilesystemInfo info;
  ASSERT_NO_FAILURES(test->GetFsInfo(&info));

  const char kFsName[] = "blobfs";
  const char* name = reinterpret_cast<const char*>(info.name.data());
  ASSERT_STR_EQ(kFsName, name);
  ASSERT_LE(info.used_nodes, info.total_nodes, "Used nodes greater than free nodes");
  ASSERT_LE(info.used_bytes, info.total_bytes, "Used bytes greater than free bytes");
}

}  // namespace

void BlobfsTest::CheckInfo() {
  CheckBlobfsInfo(this);
}

void BlobfsFixedDiskSizeTest::CheckInfo() {
  CheckBlobfsInfo(this);
}

void BlobfsTestWithFvm::CheckInfo() {
  CheckBlobfsInfo(this);
}

void BlobfsTestWithFvm::CheckPartitionSize() {
  // Minimum size required by ResizePartition test:
  const size_t kMinDataSize = 507 * kTestFvmSliceSize;
  const size_t kMinFvmSize =
      fvm::MetadataSize(kMinDataSize, kTestFvmSliceSize) * 2 + kMinDataSize;  // ~8.5mb
  ASSERT_GE(environment_->disk_size(), kMinFvmSize, "Insufficient disk space for FVM tests");
}

void BlobfsFixedDiskSizeTestWithFvm::CheckInfo() {
  CheckBlobfsInfo(this);
}

void MakeBlob(const blobfs::BlobInfo* info, fbl::unique_fd* fd) {
  fd->reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(*fd, "Failed to create blob");
  ASSERT_EQ(ftruncate(fd->get(), info->size_data), 0);
  ASSERT_EQ(blobfs::StreamAll(write, fd->get(), info->data.get(), info->size_data), 0,
            "Failed to write Data");
  ASSERT_NO_FAILURES(blobfs::VerifyContents(fd->get(), info->data.get(), info->size_data));
}
