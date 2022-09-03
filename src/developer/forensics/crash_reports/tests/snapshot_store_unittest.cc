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
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"

namespace forensics::crash_reports {
namespace {

using testing::HasSubstr;
using testing::IsEmpty;
using testing::IsSupersetOf;
using testing::Pair;
using testing::UnorderedElementsAreArray;

feedback::Annotations BuildFeedbackAnnotations(
    const std::map<std::string, std::string>& annotations) {
  feedback::Annotations ret_annotations;
  for (const auto& [key, value] : annotations) {
    ret_annotations.insert({key, value});
  }
  return ret_annotations;
}

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

const std::map<std::string, std::string> kDefaultAnnotations = {
    {"annotation.key.one", "annotation.value.one"},
    {"annotation.key.two", "annotation.value.two"},
};

const feedback::Annotations kDefaultFeedbackAnnotations =
    BuildFeedbackAnnotations(kDefaultAnnotations);

class SnapshotStoreTest : public UnitTestFixture {
 public:
  SnapshotStoreTest()
      : garbage_collected_snapshots_path_(
            files::JoinPath(tmp_dir_.path(), "garbage_collected_snapshots.txt")) {
    snapshot_store_ =
        std::make_unique<SnapshotStore>(&annotation_manager_, garbage_collected_snapshots_path_,
                                        StorageSize::Megabytes(1u), StorageSize::Megabytes(1u));
  }

 protected:
  void SetUpSnapshotStore(StorageSize max_annotations_size = StorageSize::Megabytes(1),
                          StorageSize max_archives_size = StorageSize::Megabytes(1)) {
    snapshot_store_ =
        std::make_unique<SnapshotStore>(&annotation_manager_, garbage_collected_snapshots_path_,
                                        max_annotations_size, max_archives_size);
  }

  fuchsia::feedback::Attachment GetDefaultAttachment() {
    fuchsia::feedback::Attachment snapshot;
    snapshot.key = kDefaultArchiveKey;
    FX_CHECK(fsl::VmoFromString("", &snapshot.value));
    return snapshot;
  }

  void AddDefaultSnapshot(const SnapshotUuid& uuid = kTestUuid) {
    snapshot_store_->StartSnapshot(uuid);
    snapshot_store_->IncrementClientCount(uuid);
    snapshot_store_->AddSnapshotData(uuid, kDefaultFeedbackAnnotations, GetDefaultAttachment());
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

using SnapshotStoreDeathTest = SnapshotStoreTest;

TEST_F(SnapshotStoreTest, Check_AddEmpty) {
  snapshot_store_->StartSnapshot(kTestUuid);

  // This next call should not crash if AddEmpty functions correctly.
  snapshot_store_->IncrementClientCount(kTestUuid);
}

TEST_F(SnapshotStoreDeathTest, Check_GetSnapshotNeverAdded) {
  snapshot_store_->StartSnapshot(kTestUuid);

  // Ask for snapshot before adding it.
  ASSERT_DEATH(snapshot_store_->GetSnapshot(kTestUuid), HasSubstr("Annotations can't be null"));
}

TEST_F(SnapshotStoreTest, Check_GetSnapshot) {
  snapshot_store_->StartSnapshot(kTestUuid);
  snapshot_store_->AddSnapshotData(kTestUuid, kDefaultFeedbackAnnotations, GetDefaultAttachment());

  auto snapshot = AsManaged(snapshot_store_->GetSnapshot(kTestUuid));
  ASSERT_TRUE(snapshot.LockArchive());

  EXPECT_THAT(snapshot.Annotations(), IsSupersetOf(Vector(kDefaultAnnotations)));
  EXPECT_THAT(snapshot.PresenceAnnotations(), IsEmpty());
  EXPECT_EQ(snapshot.LockArchive()->key, kDefaultArchiveKey);
}

TEST_F(SnapshotStoreTest, Check_SetsPresenceAnnotations) {
  AddDefaultSnapshot();

  auto snapshot = AsManaged(snapshot_store_->GetSnapshot(kTestUuid));
  ASSERT_TRUE(snapshot.LockArchive());

  std::map<std::string, std::string> annotations(kDefaultAnnotations.begin(),
                                                 kDefaultAnnotations.end());
  annotations["debug.snapshot.shared-request.num-clients"] = std::to_string(1);
  annotations["debug.snapshot.shared-request.uuid"] = kTestUuid;

  EXPECT_THAT(snapshot.Annotations(), UnorderedElementsAreArray(Vector(annotations)));
  EXPECT_THAT(snapshot.PresenceAnnotations(), IsEmpty());
}

TEST_F(SnapshotStoreTest, Check_AnnotationsMaxSizeIsEnforced) {
  // Initialize the manager to only hold the default annotations and the debug annotations.
  SetUpSnapshotStore(StorageSize::Bytes(256), StorageSize::Megabytes(1));
  AddDefaultSnapshot();

  EXPECT_FALSE(snapshot_store_->SizeLimitsExceeded());
  snapshot_store_->EnforceSizeLimits(kTestUuid);

  EXPECT_TRUE(snapshot_store_->SnapshotExists(kTestUuid));
  ASSERT_TRUE(AsManaged(snapshot_store_->GetSnapshot(kTestUuid)).LockArchive());

  const SnapshotUuid kTestUuid2 = kTestUuid + "2";
  AddDefaultSnapshot(kTestUuid2);

  EXPECT_TRUE(snapshot_store_->SizeLimitsExceeded());

  snapshot_store_->EnforceSizeLimits(kTestUuid);
  EXPECT_FALSE(snapshot_store_->SnapshotExists(kTestUuid));

  snapshot_store_->EnforceSizeLimits(kTestUuid2);
  EXPECT_TRUE(snapshot_store_->SnapshotExists(kTestUuid2));

  ASSERT_TRUE(AsManaged(snapshot_store_->GetSnapshot(kTestUuid2)).LockArchive());

  EXPECT_THAT(AsMissing(snapshot_store_->GetSnapshot(kTestUuid)).PresenceAnnotations(),
              UnorderedElementsAreArray({
                  Pair("debug.snapshot.error", "garbage collected"),
                  Pair("debug.snapshot.present", "false"),
              }));

  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   kTestUuid,
                                               }));
}

TEST_F(SnapshotStoreTest, Check_ArchivesMaxSizeIsEnforced) {
  // Initialize the manager to only hold a single default snapshot archive.
  SetUpSnapshotStore(StorageSize::Megabytes(1), StorageSize::Bytes(kDefaultArchiveKey.size()));

  AddDefaultSnapshot();

  EXPECT_FALSE(snapshot_store_->SizeLimitsExceeded());
  snapshot_store_->EnforceSizeLimits(kTestUuid);
  EXPECT_TRUE(snapshot_store_->SnapshotExists(kTestUuid));
  ASSERT_TRUE(AsManaged(snapshot_store_->GetSnapshot(kTestUuid)).LockArchive());

  const SnapshotUuid kTestUuid2 = kTestUuid + "2";
  AddDefaultSnapshot(kTestUuid2);

  EXPECT_TRUE(snapshot_store_->SizeLimitsExceeded());
  snapshot_store_->EnforceSizeLimits(kTestUuid);
  snapshot_store_->EnforceSizeLimits(kTestUuid2);
  EXPECT_TRUE(snapshot_store_->SnapshotExists(kTestUuid));
  EXPECT_TRUE(snapshot_store_->SnapshotExists(kTestUuid2));

  ASSERT_TRUE(AsManaged(snapshot_store_->GetSnapshot(kTestUuid2)).LockArchive());

  EXPECT_FALSE(AsManaged(snapshot_store_->GetSnapshot(kTestUuid)).LockArchive());
  EXPECT_THAT(AsManaged(snapshot_store_->GetSnapshot(kTestUuid)).Annotations(),
              UnorderedElementsAreArray({
                  Pair("annotation.key.one", "annotation.value.one"),
                  Pair("annotation.key.two", "annotation.value.two"),
                  Pair("debug.snapshot.shared-request.num-clients", "1"),
                  Pair("debug.snapshot.shared-request.uuid", kTestUuid.c_str()),
              }));
  EXPECT_THAT(AsManaged(snapshot_store_->GetSnapshot(kTestUuid)).PresenceAnnotations(),
              UnorderedElementsAreArray({
                  Pair("debug.snapshot.error", "garbage collected"),
                  Pair("debug.snapshot.present", "false"),
              }));
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   kTestUuid,
                                               }));
}

TEST_F(SnapshotStoreTest, Check_Release) {
  AddDefaultSnapshot();

  {
    auto snapshot = AsManaged(snapshot_store_->GetSnapshot(kTestUuid));
    ASSERT_TRUE(snapshot.LockArchive());
  }

  snapshot_store_->Release(kTestUuid);
  {
    auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kTestUuid));
    EXPECT_THAT(snapshot.PresenceAnnotations(),
                UnorderedElementsAreArray({
                    Pair("debug.snapshot.error", "garbage collected"),
                    Pair("debug.snapshot.present", "false"),
                }));
  }
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   kTestUuid,
                                               }));
}

TEST_F(SnapshotStoreTest, Check_GarbageCollected) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kGarbageCollectedSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(), UnorderedElementsAreArray({
                                                  Pair("debug.snapshot.error", "garbage collected"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
}

TEST_F(SnapshotStoreTest, Check_NotPersisted) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kNotPersistedSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(), UnorderedElementsAreArray({
                                                  Pair("debug.snapshot.error", "not persisted"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
}

TEST_F(SnapshotStoreTest, Check_TimedOut) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kTimedOutSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(), UnorderedElementsAreArray({
                                                  Pair("debug.snapshot.error", "timeout"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
}

TEST_F(SnapshotStoreTest, Check_Shutdown) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kShutdownSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(), UnorderedElementsAreArray({
                                                  Pair("debug.snapshot.error", "system shutdown"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
}

TEST_F(SnapshotStoreTest, Check_UuidForNoSnapshotUuid) {
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kNoUuidSnapshotUuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(), UnorderedElementsAreArray({
                                                  Pair("debug.snapshot.error", "missing uuid"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
}

TEST_F(SnapshotStoreTest, Check_DefaultToNotPersisted) {
  const SnapshotUuid uuid("UNKNOWN");
  auto snapshot = AsMissing(snapshot_store_->GetSnapshot(uuid));
  EXPECT_THAT(snapshot.PresenceAnnotations(), UnorderedElementsAreArray({
                                                  Pair("debug.snapshot.error", "not persisted"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
}

TEST_F(SnapshotStoreTest, Check_ReadPreviouslyGarbageCollected) {
  AddDefaultSnapshot();
  {
    auto snapshot = AsManaged(snapshot_store_->GetSnapshot(kTestUuid));
    ASSERT_TRUE(snapshot.LockArchive());
  }

  snapshot_store_->Release(kTestUuid);
  {
    auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kTestUuid));
    EXPECT_THAT(snapshot.PresenceAnnotations(),
                UnorderedElementsAreArray({
                    Pair("debug.snapshot.error", "garbage collected"),
                    Pair("debug.snapshot.present", "false"),
                }));
  }
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   kTestUuid,
                                               }));

  SetUpSnapshotStore(StorageSize::Megabytes(1u), StorageSize::Megabytes(1u));
  {
    auto snapshot = AsMissing(snapshot_store_->GetSnapshot(kTestUuid));
    EXPECT_THAT(snapshot.PresenceAnnotations(),
                UnorderedElementsAreArray({
                    Pair("debug.snapshot.error", "garbage collected"),
                    Pair("debug.snapshot.present", "false"),
                }));
  }
}

}  // namespace
}  // namespace forensics::crash_reports
