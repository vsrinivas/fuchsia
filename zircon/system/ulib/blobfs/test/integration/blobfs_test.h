// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include <blobfs/format.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fs-test-utils/blobfs/blobfs.h>
#include <zxtest/zxtest.h>

#include "environment.h"

// FVM slice size used for tests.
constexpr size_t kTestFvmSliceSize = blobfs::kBlobfsBlockSize;  // 8kb.

constexpr uint8_t kTestUniqueGUID[] = {
    0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

constexpr uint8_t kTestPartGUID[] = {
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

constexpr char kMountPath[] = "/blobfs-tmp/zircon-blobfs-test";

enum class FsTestType {
    kGeneric,  // Use a generic block device.
    kFvm       // Use an FVM device.
};

class BlobfsTest : public zxtest::Test {
  public:
    explicit BlobfsTest(FsTestType type = FsTestType::kGeneric);

    // zxtest::Test interface:
    void SetUp() override;
    void TearDown() override;

    // Unmounts and remounts the filesystem.
    void Remount();

    DISALLOW_COPY_ASSIGN_AND_MOVE(BlobfsTest);

  protected:
    void Mount();
    void Unmount();
    zx_status_t CheckFs();
    void CheckInfo();

    FsTestType type_;
    Environment* environment_;
    std::string device_path_;
    bool read_only_ = false;
    bool mounted_ = false;
};

class BlobfsTestWithFvm : public BlobfsTest {
  public:
    BlobfsTestWithFvm() : BlobfsTest(FsTestType::kFvm) {}

    // zxtest::Test interface:
    void SetUp() override;
    void TearDown() override;

    DISALLOW_COPY_ASSIGN_AND_MOVE(BlobfsTestWithFvm);

  private:
    void BindFvm();
    void CreatePartition();

    std::string fvm_path_;
    std::string partition_path_;
};

// Creates an open blob with the provided Merkle tree + Data, and reads back to
// verify the data.
void MakeBlob(const fs_test_utils::BlobInfo* info, fbl::unique_fd* fd);
