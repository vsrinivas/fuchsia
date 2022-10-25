// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_persistence.h"

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::crash_reports {
namespace {

using testing::HasSubstr;

constexpr const char* kSnapshotStoreTmpPath = "/tmp/snapshots";
constexpr const char* kSnapshotStoreCachePath = "/cache/snapshots";

class SnapshotPersistenceTest : public UnitTestFixture {
 public:
  SnapshotPersistenceTest() { MakeNewPersistence(); }

 protected:
  std::string GetTmpDir() { return files::JoinPath(temp_dir_.path(), kSnapshotStoreTmpPath); }
  std::string GetCacheDir() { return files::JoinPath(temp_dir_.path(), kSnapshotStoreCachePath); }

  void MakeNewPersistence() {
    persistence_ = std::make_unique<SnapshotPersistence>(GetTmpDir(), GetCacheDir());
  }

  void WriteAttachment(const SnapshotUuid& uuid, const std::string& key, const std::string& data) {
    std::filesystem::path path(GetTmpDir());

    files::CreateDirectory(path /= uuid);
    std::ofstream(path /= key) << data;
  }

  std::unique_ptr<SnapshotPersistence> persistence_;

 private:
  files::ScopedTempDir temp_dir_;
};

using SnapshotPersistenceDeathTest = SnapshotPersistenceTest;

TEST_F(SnapshotPersistenceTest, Succeed_Get) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kDefaultArchiveValue = "snapshot.data";

  WriteAttachment(kTestUuid, feedback_data::kSnapshotFilename, kDefaultArchiveValue);

  // Make new persistence to force metadata reload.
  MakeNewPersistence();

  ASSERT_TRUE(persistence_->Contains(kTestUuid));
  const auto archive = persistence_->Get(kTestUuid);

  EXPECT_EQ(archive->key, feedback_data::kSnapshotFilename);
  EXPECT_EQ(std::string(archive->value.begin(), archive->value.end()), kDefaultArchiveValue);
}

TEST_F(SnapshotPersistenceDeathTest, Check_FailGet) {
  const SnapshotUuid kTestUuid = "test uuid";

  // Attempt to get snapshot that doesn't exist.
  ASSERT_DEATH({ persistence_->Get(kTestUuid); },
               HasSubstr("Contains() should be called before any Get()"));
}

}  // namespace
}  // namespace forensics::crash_reports
