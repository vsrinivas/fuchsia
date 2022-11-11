// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_persistence.h"

#include <lib/syslog/cpp/macros.h>

#include <filesystem>
#include <memory>

#include "gtest/gtest.h"
#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/scoped_memfs_manager.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"

namespace forensics::crash_reports {
namespace {

using ::testing::HasSubstr;

namespace fs = std::filesystem;

struct StringAttachment {
  std::string key;
  std::string value;
};

constexpr const char* kSnapshotStoreTmpPath = "/tmp/snapshots";
constexpr const char* kSnapshotStoreCachePath = "/cache/snapshots";

// Returns true if filesystem read successfully.
bool Read(const fs::path& root_dir, const SnapshotUuid& uuid, StringAttachment& archive_out) {
  const std::string path = files::JoinPath(root_dir, uuid);
  std::vector<std::string> files;

  if (!files::ReadDirContents(path, &files)) {
    return false;
  }

  for (const auto& file : files) {
    if (file == feedback_data::kSnapshotFilename) {
      std::string file_content;
      if (!files::ReadFileToString(files::JoinPath(path, file), &file_content)) {
        return false;
      }

      archive_out.key = file;
      archive_out.value = file_content;
    } else if (file == ".") {
      continue;
    } else {
      FX_LOGS(ERROR) << "Unexpected file found in snapshot persistence";
      return false;
    }
  }

  return true;
}

class SnapshotPersistenceTest : public UnitTestFixture {
 public:
  SnapshotPersistenceTest() { MakeNewPersistence(); }

 protected:
  std::string GetTmpDir() { return files::JoinPath(temp_dir_.path(), kSnapshotStoreTmpPath); }
  std::string GetCacheDir() { return files::JoinPath(temp_dir_.path(), kSnapshotStoreCachePath); }

  void MakeNewPersistence(const StorageSize max_tmp_size = StorageSize::Megabytes(1),
                          const StorageSize max_cache_size = StorageSize::Megabytes(1)) {
    persistence_ = std::make_unique<SnapshotPersistence>(
        SnapshotPersistence::Root{GetTmpDir(), max_tmp_size},
        SnapshotPersistence::Root{GetCacheDir(), max_cache_size});
  }

  bool AddArchive(const SnapshotUuid& uuid, std::string archive_value) {
    fuchsia::feedback::Attachment snapshot;
    snapshot.key = feedback_data::kSnapshotFilename;
    FX_CHECK(fsl::VmoFromString(std::move(archive_value), &snapshot.value));

    auto archive = ManagedSnapshot::Archive(snapshot);
    auto archive_size = StorageSize::Bytes(archive.key.size());
    archive_size += StorageSize::Bytes(archive.value.size());

    return persistence_->Add(uuid, archive, archive_size, /*only_consider_tmp=*/false);
  }

  std::unique_ptr<SnapshotPersistence> persistence_;

 private:
  files::ScopedTempDir temp_dir_;
};

using SnapshotPersistenceDeathTest = SnapshotPersistenceTest;

TEST_F(SnapshotPersistenceTest, Succeed_AddDefaultsToCache) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  ASSERT_TRUE(AddArchive(kTestUuid, kArchiveValue));
  ASSERT_TRUE(persistence_->Contains(kTestUuid));

  StringAttachment archive;
  ASSERT_TRUE(Read(GetCacheDir(), kTestUuid, archive));

  EXPECT_EQ(archive.key, feedback_data::kSnapshotFilename);
  EXPECT_EQ(archive.value, kArchiveValue);
}

TEST_F(SnapshotPersistenceTest, Succeed_FallbackToTmpIfCacheFull) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  // Make /cache not have room for any archives.
  MakeNewPersistence(/*max_tmp_size=*/StorageSize::Megabytes(1),
                     /*max_cache_size=*/StorageSize::Bytes(0));

  ASSERT_TRUE(AddArchive(kTestUuid, kArchiveValue));
  ASSERT_TRUE(persistence_->Contains(kTestUuid));

  StringAttachment archive;
  ASSERT_TRUE(Read(GetTmpDir(), kTestUuid, archive));

  EXPECT_EQ(archive.key, feedback_data::kSnapshotFilename);
  EXPECT_EQ(archive.value, kArchiveValue);
}

TEST_F(SnapshotPersistenceTest, Succeed_FallbackToTmpIfCacheWriteFails) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  // Create a file under the cache directory where the next snapshot directory would be created.
  // This will cause the write to cache to fail.
  const std::string snapshot_dir = files::JoinPath(GetCacheDir(), kTestUuid);
  ASSERT_TRUE(files::WriteFile(snapshot_dir, "n/a"));

  ASSERT_TRUE(AddArchive(kTestUuid, kArchiveValue));
  ASSERT_TRUE(persistence_->Contains(kTestUuid));

  StringAttachment archive;
  ASSERT_TRUE(Read(GetTmpDir(), kTestUuid, archive));

  EXPECT_EQ(archive.key, feedback_data::kSnapshotFilename);
  EXPECT_EQ(archive.value, kArchiveValue);
}

TEST_F(SnapshotPersistenceTest, Check_PersistenceFull) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  MakeNewPersistence(/*max_tmp_size=*/StorageSize::Bytes(0),
                     /*max_cache_size=*/StorageSize::Bytes(0));

  EXPECT_FALSE(AddArchive(kTestUuid, kArchiveValue));
  EXPECT_FALSE(persistence_->Contains(kTestUuid));

  StringAttachment archive;
  EXPECT_FALSE(Read(GetTmpDir(), kTestUuid, archive));
  EXPECT_FALSE(Read(GetCacheDir(), kTestUuid, archive));
}

TEST_F(SnapshotPersistenceTest, Succeed_Get) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  ASSERT_TRUE(AddArchive(kTestUuid, kArchiveValue));

  const auto archive = persistence_->Get(kTestUuid);

  EXPECT_EQ(archive->key, feedback_data::kSnapshotFilename);
  EXPECT_EQ(std::string(archive->value.begin(), archive->value.end()), kArchiveValue);
}

TEST_F(SnapshotPersistenceDeathTest, Check_FailGet) {
  const SnapshotUuid kTestUuid = "test uuid";

  // Attempt to get snapshot that doesn't exist.
  ASSERT_DEATH({ persistence_->Get(kTestUuid); },
               HasSubstr("Contains() should be called before any Get()"));
}

TEST_F(SnapshotPersistenceTest, Check_RebuildsMetadata) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  ASSERT_TRUE(AddArchive(kTestUuid, kArchiveValue));
  ASSERT_TRUE(persistence_->Contains(kTestUuid));

  MakeNewPersistence();

  ASSERT_TRUE(persistence_->Contains(kTestUuid));

  const auto archive = persistence_->Get(kTestUuid);

  EXPECT_EQ(archive->key, feedback_data::kSnapshotFilename);
  EXPECT_EQ(std::string(archive->value.begin(), archive->value.end()), kArchiveValue);
}

TEST_F(SnapshotPersistenceTest, Check_UsesTmpUntilCacheReady) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  // Use directory that |scoped_mem_fs| can create using ScopedMemFsManager::Create, but |store_|
  // can't create using files::CreateDirectory
  const std::string cache_root = "/cache/delayed/path";
  testing::ScopedMemFsManager scoped_mem_fs;
  persistence_ = std::make_unique<SnapshotPersistence>(
      SnapshotPersistence::Root{GetTmpDir(), StorageSize::Megabytes(1)},
      SnapshotPersistence::Root{cache_root, StorageSize::Megabytes(1)});

  // The first report should be placed under the tmp directory because the cache directory isn't
  // ready.
  ASSERT_TRUE(AddArchive(kTestUuid, kArchiveValue));

  StringAttachment archive;
  ASSERT_TRUE(Read(GetTmpDir(), kTestUuid, archive));

  EXPECT_EQ(archive.key, feedback_data::kSnapshotFilename);
  EXPECT_EQ(archive.value, kArchiveValue);

  // Create the cache directory so it can be used for the next report.
  scoped_mem_fs.Create(cache_root);

  // The second report should be placed under the cache directory.
  const SnapshotUuid kTestUuid2 = "test uuid 2";
  ASSERT_TRUE(AddArchive(kTestUuid2, kArchiveValue));

  StringAttachment archive2;
  ASSERT_TRUE(Read(cache_root, kTestUuid2, archive2));

  EXPECT_EQ(archive2.key, feedback_data::kSnapshotFilename);
  EXPECT_EQ(archive2.value, kArchiveValue);
}

TEST_F(SnapshotPersistenceTest, Succeed_Delete) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  ASSERT_TRUE(AddArchive(kTestUuid, kArchiveValue));
  ASSERT_TRUE(persistence_->Contains(kTestUuid));

  StringAttachment archive;
  ASSERT_TRUE(Read(GetCacheDir(), kTestUuid, archive));
  EXPECT_FALSE(archive.key.empty());
  EXPECT_FALSE(archive.value.empty());

  ASSERT_TRUE(persistence_->Delete(kTestUuid));
  EXPECT_FALSE(persistence_->Contains(kTestUuid));
  EXPECT_FALSE(Read(GetCacheDir(), kTestUuid, archive));
}

TEST_F(SnapshotPersistenceTest, Succeed_MoveToTmp) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  ASSERT_TRUE(AddArchive(kTestUuid, kArchiveValue));
  ASSERT_EQ(persistence_->SnapshotLocation(kTestUuid), ItemLocation::kCache);

  StringAttachment archive;
  ASSERT_TRUE(Read(GetCacheDir(), kTestUuid, archive));
  EXPECT_FALSE(archive.key.empty());
  EXPECT_FALSE(archive.value.empty());

  persistence_->MoveToTmp(kTestUuid);
  ASSERT_EQ(persistence_->SnapshotLocation(kTestUuid), ItemLocation::kTmp);

  StringAttachment archive2;
  ASSERT_TRUE(Read(GetTmpDir(), kTestUuid, archive2));
  EXPECT_FALSE(archive2.key.empty());
  EXPECT_FALSE(archive2.value.empty());

  EXPECT_FALSE(Read(GetCacheDir(), kTestUuid, archive2));
}

TEST_F(SnapshotPersistenceDeathTest, Check_FailMoveFromTmpToTmp) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  MakeNewPersistence(/*max_tmp_size=*/StorageSize::Megabytes(1),
                     /*max_cache_size=*/StorageSize::Bytes(0));

  ASSERT_TRUE(AddArchive(kTestUuid, kArchiveValue));
  ASSERT_EQ(persistence_->SnapshotLocation(kTestUuid), ItemLocation::kTmp);

  ASSERT_DEATH({ persistence_->MoveToTmp(kTestUuid); },
               HasSubstr("MoveToTmp() will only move snapshots from /cache to /tmp"));
}

TEST_F(SnapshotPersistenceTest, Check_AddOnlyConsiderTmp) {
  const SnapshotUuid kTestUuid = "test uuid";
  const std::string kArchiveValue = "snapshot.data";

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = feedback_data::kSnapshotFilename;
  FX_CHECK(fsl::VmoFromString(kArchiveValue, &snapshot.value));

  const auto expected_archive = ManagedSnapshot::Archive(snapshot);
  auto expected_archive_size = StorageSize::Bytes(expected_archive.key.size());
  expected_archive_size += StorageSize::Bytes(expected_archive.value.size());

  persistence_->Add(kTestUuid, expected_archive, expected_archive_size, /*only_consider_tmp=*/true);

  StringAttachment archive;
  ASSERT_TRUE(Read(GetTmpDir(), kTestUuid, archive));
  EXPECT_FALSE(archive.key.empty());
  EXPECT_FALSE(archive.value.empty());

  EXPECT_FALSE(Read(GetCacheDir(), kTestUuid, archive));
}

}  // namespace
}  // namespace forensics::crash_reports
