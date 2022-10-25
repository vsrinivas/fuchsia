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
#include "src/lib/files/directory.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::crash_reports {
namespace {

class SnapshotPersistenceMetadataTest : public ::testing::Test {
 public:
  SnapshotPersistenceMetadataTest() : metadata_(tmp_dir_.path()) {}

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
  SnapshotPersistenceMetadata metadata("/cache/delayed/path");
  ASSERT_FALSE(metadata.IsDirectoryUsable());

  scoped_mem_fs.Create("/cache/delayed/path");
  metadata.RecreateFromFilesystem();
  EXPECT_TRUE(metadata.IsDirectoryUsable());
}

}  // namespace
}  // namespace forensics::crash_reports
