// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"

#include <blobfs/blob-layout.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace blobfs {

fs_test::TestFilesystemOptions BlobfsDefaultTestParam() {
  auto options = fs_test::TestFilesystemOptions::BlobfsWithoutFvm();
  options.description = "Blobfs";
  return options;
}

fs_test::TestFilesystemOptions BlobfsWithFvmTestParam() {
  auto options = fs_test::TestFilesystemOptions::DefaultBlobfs();
  options.description = "BlobfsWithFvm";
  return options;
}

fs_test::TestFilesystemOptions BlobfsWithCompactLayoutTestParam() {
  auto options = BlobfsDefaultTestParam();
  options.description = "BlobfsWithCompactLayout";
  options.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd;
  return options;
}

fs_test::TestFilesystemOptions BlobfsWithFixedDiskSizeTestParam(uint64_t disk_size) {
  auto options = BlobfsDefaultTestParam();
  options.description = "BlobfsWithFixedDiskSize";
  options.device_block_count = disk_size / options.device_block_size;
  return options;
}

}  // namespace blobfs
