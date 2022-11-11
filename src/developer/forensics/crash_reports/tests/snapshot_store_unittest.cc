// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_store.h"

#include <fstream>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/feedback/cpp/fidl.h"
#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"

namespace forensics::crash_reports {
namespace {

using testing::HasSubstr;
using testing::Pair;
using testing::UnorderedElementsAreArray;

ManagedSnapshot AsManaged(Snapshot snapshot) {
  FX_CHECK(std::holds_alternative<ManagedSnapshot>(snapshot));
  return std::get<ManagedSnapshot>(snapshot);
}

MissingSnapshot AsMissing(Snapshot snapshot) {
  FX_CHECK(std::holds_alternative<MissingSnapshot>(snapshot));
  return std::get<MissingSnapshot>(snapshot);
}

template <typename K, typename V>
auto Vector(const std::map<K, V>& annotations) {
  std::vector matchers{Pair(K(), V())};
  matchers.clear();

  for (const auto& [k, v] : annotations) {
    matchers.push_back(Pair(k, v));
  }

  return matchers;
}

const std::string kDefaultArchiveKey = "snapshot.key";
const SnapshotUuid kTestUuid = "test uuid";

class SnapshotStoreTest : public UnitTestFixture {
 public:
  SnapshotStoreTest()
      : garbage_collected_snapshots_path_(
            files::JoinPath(tmp_dir_.path(), "garbage_collected_snapshots.txt")) {
    SetUpSnapshotStore();
  }

 protected:
  void SetUpSnapshotStore(StorageSize max_archives_size = StorageSize::Megabytes(1),
                          StorageSize max_tmp_size = StorageSize::Bytes(0),
                          StorageSize max_cache_size = StorageSize::Bytes(0)) {
    snapshot_store_ = std::make_unique<SnapshotStore>(
        &annotation_manager_, garbage_collected_snapshots_path_,
        /*temp_root=*/
        SnapshotPersistence::Root{files::JoinPath(tmp_dir_.path(), kSnapshotStoreTmpPath),
                                  max_tmp_size},
        /*persistent_root=*/
        SnapshotPersistence::Root{files::JoinPath(tmp_dir_.path(), kSnapshotStoreCachePath),
                                  max_cache_size},
        max_archives_size);
  }

  fuchsia::feedback::Attachment GetDefaultAttachment() {
    fuchsia::feedback::Attachment snapshot;
    snapshot.key = kDefaultArchiveKey;
    FX_CHECK(fsl::VmoFromString("", &snapshot.value));
    return snapshot;
  }

  void AddDefaultSnapshot(const SnapshotUuid& uuid = kTestUuid) {
    snapshot_store_->AddSnapshot(uuid, GetDefaultAttachment());
  }

  std::set<std::string> ReadGarbageCollectedSnapshots() {
    std::set<std::string> garbage_collected_snapshots;

    std::ifstream file(garbage_collected_snapshots_path_);
    for (std::string uuid; getline(file, uuid);) {
      garbage_collected_snapshots.insert(uuid);
    }

    return garbage_collected_snapshots;
  }

  feedback::AnnotationManager annotation_manager_{dispatcher(), {}};
  files::ScopedTempDir tmp_dir_;
  std::string garbage_collected_snapshots_path_;
  std::unique_ptr<SnapshotStore> snapshot_store_;
};

TEST_F(SnapshotStoreTest, Check_GetSnapshot) {
  snapshot_store_->AddSnapshot(kTestUuid, GetDefaultAttachment());

  auto snapshot = AsManaged(snapshot_store_->GetSnapshot(kTestUuid));
  ASSERT_TRUE(snapshot.LockArchive());
  EXPECT_EQ(snapshot.LockArchive()->key, kDefaultArchiveKey);
}

TEST_F(SnapshotStoreTest, Check_ArchivesMaxSizeIsEnforced) {
  // Initialize the manager to only hold a single default snapshot archive.
  SetUpSnapshotStore(StorageSize::Bytes(kDefaultArchiveKey.size()));

  AddDefaultSnapshot();

  EXPECT_FALSE(snapshot_store_->SizeLimitsExceeded());
  EXPECT_EQ(snapshot_store_->SnapshotLocation(kTestUuid), ItemLocation::kMemory);
  ASSERT_TRUE(AsManaged(snapshot_store_->GetSnapshot(kTestUuid)).LockArchive());

  const SnapshotUuid kTestUuid2 = kTestUuid + "2";
  AddDefaultSnapshot(kTestUuid2);

  EXPECT_FALSE(snapshot_store_->SizeLimitsExceeded());
  EXPECT_FALSE(snapshot_store_->SnapshotLocation(kTestUuid).has_value());
  EXPECT_EQ(snapshot_store_->SnapshotLocation(kTestUuid2), ItemLocation::kMemory);

  ASSERT_TRUE(AsManaged(snapshot_store_->GetSnapshot(kTestUuid2)).LockArchive());

  EXPECT_THAT(AsMissing(snapshot_store_->GetSnapshot(kTestUuid)).PresenceAnnotations(),
              UnorderedElementsAreArray({
                  Pair(feedback::kDebugSnapshotErrorKey, "garbage collected"),
                  Pair(feedback::kDebugSnapshotPresentKey, "false"),
              }));
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   kTestUuid,
                                               }));
}

TEST_F(SnapshotStoreTest, Check_Delete) {
  AddDefaultSnapshot();

  {
    auto snapshot = AsManaged(snapshot_store_->GetSnapshot(kTestUuid));
    ASSERT_TRUE(snapshot.LockArchive());
  }

  snapshot_store_->DeleteSnapshot(kTestUuid);
  {
    auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kTestUuid));
    EXPECT_THAT(snapshot.PresenceAnnotations(),
                UnorderedElementsAreArray({
                    Pair(feedback::kDebugSnapshotErrorKey, "garbage collected"),
                    Pair(feedback::kDebugSnapshotPresentKey, "false"),
                }));
  }
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   kTestUuid,
                                               }));
}

TEST_F(SnapshotStoreTest, Check_GarbageCollected) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kGarbageCollectedSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(),
              UnorderedElementsAreArray({
                  Pair(feedback::kDebugSnapshotErrorKey, "garbage collected"),
                  Pair(feedback::kDebugSnapshotPresentKey, "false"),
              }));
}

TEST_F(SnapshotStoreTest, Check_NotPersisted) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kNotPersistedSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(),
              UnorderedElementsAreArray({
                  Pair(feedback::kDebugSnapshotErrorKey, "not persisted"),
                  Pair(feedback::kDebugSnapshotPresentKey, "false"),
              }));
}

TEST_F(SnapshotStoreTest, Check_TimedOut) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kTimedOutSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(), UnorderedElementsAreArray({
                                                  Pair(feedback::kDebugSnapshotErrorKey, "timeout"),
                                                  Pair(feedback::kDebugSnapshotPresentKey, "false"),
                                              }));
}

TEST_F(SnapshotStoreTest, Check_Shutdown) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kShutdownSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(),
              UnorderedElementsAreArray({
                  Pair(feedback::kDebugSnapshotErrorKey, "system shutdown"),
                  Pair(feedback::kDebugSnapshotPresentKey, "false"),
              }));
}

TEST_F(SnapshotStoreTest, Check_UuidForNoSnapshotUuid) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kNoUuidSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(),
              UnorderedElementsAreArray({
                  Pair(feedback::kDebugSnapshotErrorKey, "missing uuid"),
                  Pair(feedback::kDebugSnapshotPresentKey, "false"),
              }));
}

TEST_F(SnapshotStoreTest, Check_DefaultToNotPersisted) {
  const SnapshotUuid uuid("UNKNOWN");
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(uuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(),
              UnorderedElementsAreArray({
                  Pair(feedback::kDebugSnapshotErrorKey, "not persisted"),
                  Pair(feedback::kDebugSnapshotPresentKey, "false"),
              }));
}

TEST_F(SnapshotStoreTest, Check_ReadPreviouslyGarbageCollected) {
  AddDefaultSnapshot();
  {
    auto snapshot = AsManaged(snapshot_store_->GetSnapshot(kTestUuid));
    ASSERT_TRUE(snapshot.LockArchive());
  }

  snapshot_store_->DeleteSnapshot(kTestUuid);
  {
    auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kTestUuid));
    EXPECT_THAT(snapshot.PresenceAnnotations(),
                UnorderedElementsAreArray({
                    Pair(feedback::kDebugSnapshotErrorKey, "garbage collected"),
                    Pair(feedback::kDebugSnapshotPresentKey, "false"),
                }));
  }
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   kTestUuid,
                                               }));

  SetUpSnapshotStore(StorageSize::Megabytes(1u));
  {
    auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kTestUuid));
    EXPECT_THAT(snapshot.PresenceAnnotations(),
                UnorderedElementsAreArray({
                    Pair(feedback::kDebugSnapshotErrorKey, "garbage collected"),
                    Pair(feedback::kDebugSnapshotPresentKey, "false"),
                }));
  }
}

TEST_F(SnapshotStoreTest, Check_RemovesFromInsertionOrder) {
  // Initialize SnapshotStore to only hold 2 default snapshot archives.
  SetUpSnapshotStore(StorageSize::Bytes(2 * kDefaultArchiveKey.size()));

  AddDefaultSnapshot();

  const SnapshotUuid kTestUuid2 = kTestUuid + "2";
  AddDefaultSnapshot(kTestUuid2);

  ASSERT_FALSE(snapshot_store_->SizeLimitsExceeded());
  ASSERT_EQ(snapshot_store_->SnapshotLocation(kTestUuid), ItemLocation::kMemory);
  ASSERT_EQ(snapshot_store_->SnapshotLocation(kTestUuid2), ItemLocation::kMemory);

  // Delete snapshots in different order than they were added.
  snapshot_store_->DeleteSnapshot(kTestUuid2);
  ASSERT_FALSE(snapshot_store_->SnapshotLocation(kTestUuid2).has_value());

  // Trigger garbage collection twice by going over size limit. If |kTestUuid2| wasn't removed from
  // insertion_order_ (a FIFO queue), this would cause a CHECK-FAIL crash.
  const SnapshotUuid kTestUuid3 = kTestUuid + "3";
  AddDefaultSnapshot(kTestUuid3);

  const SnapshotUuid kTestUuid4 = kTestUuid + "4";
  AddDefaultSnapshot(kTestUuid4);

  const SnapshotUuid kTestUuid5 = kTestUuid + "5";
  AddDefaultSnapshot(kTestUuid5);

  EXPECT_FALSE(snapshot_store_->SizeLimitsExceeded());
  EXPECT_FALSE(snapshot_store_->SnapshotLocation(kTestUuid).has_value());
  EXPECT_FALSE(snapshot_store_->SnapshotLocation(kTestUuid3).has_value());
  EXPECT_EQ(snapshot_store_->SnapshotLocation(kTestUuid4), ItemLocation::kMemory);
  EXPECT_EQ(snapshot_store_->SnapshotLocation(kTestUuid5), ItemLocation::kMemory);
}

TEST_F(SnapshotStoreTest, Check_MoveToPersistence) {
  // Initialize SnapshotStore to only hold 1 default archive in memory.
  SetUpSnapshotStore(/*max_archives_size=*/StorageSize::Bytes(kDefaultArchiveKey.size()),
                     /*max_tmp_size=*/StorageSize::Bytes(0),
                     /*max_cache_size=*/StorageSize::Megabytes(1));

  AddDefaultSnapshot();

  EXPECT_EQ(snapshot_store_->SnapshotLocation(kTestUuid), ItemLocation::kMemory);

  snapshot_store_->MoveToPersistence(kTestUuid, /*only_consider_tmp=*/false);

  ASSERT_EQ(snapshot_store_->SnapshotLocation(kTestUuid), ItemLocation::kCache);

  // Trigger garbage collection by going over size limit. This will verify that MoveToPersistence
  // removed |kTestUuid| from |insertion_order_|.
  const SnapshotUuid kTestUuid2 = kTestUuid + "2";
  AddDefaultSnapshot(kTestUuid2);

  const SnapshotUuid kTestUuid3 = kTestUuid + "3";
  AddDefaultSnapshot(kTestUuid3);

  EXPECT_TRUE(snapshot_store_->SnapshotLocation(kTestUuid).has_value());
  EXPECT_FALSE(snapshot_store_->SnapshotLocation(kTestUuid2).has_value());
  EXPECT_TRUE(snapshot_store_->SnapshotLocation(kTestUuid3).has_value());
}

}  // namespace
}  // namespace forensics::crash_reports
