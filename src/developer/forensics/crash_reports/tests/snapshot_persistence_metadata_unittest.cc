// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_persistence_metadata.h"

#include <filesystem>
#include <fstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/testing/scoped_memfs_manager.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::crash_reports {
namespace {

class SnapshotPersistenceMetadataTest : public ::testing::Test {
 public:
  SnapshotPersistenceMetadataTest() : metadata_(tmp_dir_.path(), StorageSize::Megabytes(1)) {}

 protected:
  void WriteAttachment(const SnapshotUuid& uuid, const std::string& key, const std::string& data) {
    std::filesystem::path path(tmp_dir_.path());

    files::CreateDirectory(path /= uuid);
    std::ofstream(path /= key) << data;
  }

  std::string SnapshotPath(const SnapshotUuid& uuid) {
    return std::filesystem::path(tmp_dir_.path()) / uuid;
  }

  SnapshotPersistenceMetadata& metadata() { return metadata_; }

 private:
  files::ScopedTempDir tmp_dir_;
  SnapshotPersistenceMetadata metadata_;
};

TEST_F(SnapshotPersistenceMetadataTest, Contains) {
  const std::string kUuid = "uuid1";
  const std::string kValue = "value";

  ASSERT_FALSE(metadata().Contains(kUuid));

  WriteAttachment(kUuid, "key 1", kValue);
  metadata().RecreateFromFilesystem();

  EXPECT_TRUE(metadata().Contains(kUuid));
}

TEST_F(SnapshotPersistenceMetadataTest, SnapshotDirectory) {
  const std::string kUuid = "uuid1";
  const std::string kValue = "value";

  WriteAttachment(kUuid, "key 1", kValue);
  metadata().RecreateFromFilesystem();

  EXPECT_EQ(metadata().SnapshotDirectory(kUuid), SnapshotPath(kUuid));
}

TEST_F(SnapshotPersistenceMetadataTest, RecreateFromFilesystem_FailsInitially) {
  testing::ScopedMemFsManager scoped_mem_fs;
  SnapshotPersistenceMetadata metadata("/cache/delayed/path", StorageSize::Megabytes(1));
  ASSERT_FALSE(metadata.IsDirectoryUsable());

  scoped_mem_fs.Create("/cache/delayed/path");
  metadata.RecreateFromFilesystem();
  EXPECT_TRUE(metadata.IsDirectoryUsable());
}

TEST_F(SnapshotPersistenceMetadataTest, AddAndDelete) {
  const SnapshotUuid kUuid1 = "uuid1";
  const StorageSize archive_size = StorageSize::Bytes(10);
  const StorageSize old_metadata_size = metadata().CurrentSize();
  const StorageSize old_metadata_remaining_space = metadata().RemainingSpace();

  metadata().Add(kUuid1, archive_size, "key 1");

  ASSERT_TRUE(metadata().Contains(kUuid1));
  EXPECT_EQ(metadata().CurrentSize(), old_metadata_size + archive_size);
  EXPECT_EQ(metadata().RemainingSpace(), old_metadata_remaining_space - archive_size);

  metadata().Delete(kUuid1);

  EXPECT_FALSE(metadata().Contains(kUuid1));
  EXPECT_EQ(metadata().CurrentSize(), old_metadata_size);
  EXPECT_EQ(metadata().RemainingSpace(), old_metadata_remaining_space);
}

}  // namespace
}  // namespace forensics::crash_reports
