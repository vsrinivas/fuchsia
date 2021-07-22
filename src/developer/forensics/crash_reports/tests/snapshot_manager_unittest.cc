// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_manager.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/errors.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/data_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace crash_reports {
namespace {

using testing::Contains;
using testing::IsEmpty;
using testing::IsSupersetOf;
using testing::Pair;
using testing::UnorderedElementsAreArray;

constexpr zx::duration kWindow{zx::min(1)};

const std::map<std::string, std::string> kDefaultAnnotations = {
    {"annotation.key.one", "annotation.value.one"},
    {"annotation.key.two", "annotation.value.two"},
};

const std::string kDefaultArchiveKey = "snapshot.key";

template <typename K, typename V>
auto Vector(const std::map<K, V>& annotations) {
  std::vector matchers{Pair(K(), V())};
  matchers.clear();

  for (const auto& [k, v] : annotations) {
    matchers.push_back(Pair(k, v));
  }

  return matchers;
}

class SnapshotManagerTest : public UnitTestFixture {
 public:
  SnapshotManagerTest()
      : UnitTestFixture(),
        clock_(),
        executor_(dispatcher()),
        snapshot_manager_(nullptr),
        path_(files::JoinPath(tmp_dir_.path(), "garbage_collected_snapshots.txt")) {}

 protected:
  void SetUpDefaultSnapshotManager() {
    SetUpSnapshotManager(StorageSize::Megabytes(1u), StorageSize::Megabytes(1u));
  }

  void SetUpSnapshotManager(StorageSize max_annotations_size, StorageSize max_archives_size) {
    FX_CHECK(data_provider_server_);
    clock_.Set(zx::time(0u));
    snapshot_manager_ =
        std::make_unique<SnapshotManager>(dispatcher(), &clock_, data_provider_server_.get(),
                                          kWindow, path_, max_annotations_size, max_archives_size);
  }

  std::set<std::string> ReadGarbageCollectedSnapshots() {
    std::set<std::string> garbage_collected_snapshots;

    std::ifstream file(path_);
    for (std::string uuid; getline(file, uuid);) {
      garbage_collected_snapshots.insert(uuid);
    }

    return garbage_collected_snapshots;
  }

  void ClearGarbageCollectedSnapshots() { files::DeletePath(path_, /*recursive=*/true); }

  void SetUpDefaultDataProviderServer() {
    SetUpDataProviderServer(
        std::make_unique<stubs::DataProvider>(kDefaultAnnotations, kDefaultArchiveKey));
  }

  void SetUpDataProviderServer(std::unique_ptr<stubs::DataProviderBase> data_provider_server) {
    data_provider_server_ = std::move(data_provider_server);
  }

  void ScheduleGetSnapshotUuidAndThen(const zx::duration timeout,
                                      ::fit::function<void(const std::string&)> and_then) {
    executor_.schedule_task(
        snapshot_manager_->GetSnapshotUuid(timeout).and_then(std::move(and_then)).or_else([]() {
          FX_CHECK(false);
        }));
  }

  void CloseConnection() { data_provider_server_->CloseConnection(); }

  bool is_server_bound() { return data_provider_server_->IsBound(); }

  timekeeper::TestClock clock_;
  async::Executor executor_;
  std::unique_ptr<SnapshotManager> snapshot_manager_;

 private:
  std::unique_ptr<stubs::DataProviderBase> data_provider_server_;
  files::ScopedTempDir tmp_dir_;
  std::string path_;
};

TEST_F(SnapshotManagerTest, Check_GetSnapshotUuid) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  // |uuid| should only have a value once |kWindow| has passed.
  RunLoopUntilIdle();
  ASSERT_FALSE(uuid.has_value());

  RunLoopFor(kWindow);
  ASSERT_TRUE(uuid.has_value());
}

TEST_F(SnapshotManagerTest, Check_GetSnapshotUuidRequestsCombined) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  const size_t kNumRequests{5u};

  size_t num_uuid1{0};
  std::optional<std::string> uuid1{std::nullopt};
  for (size_t i = 0; i < kNumRequests; ++i) {
    ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                   ([&uuid1, &num_uuid1](const std::string& new_uuid) {
                                     if (!uuid1.has_value()) {
                                       uuid1 = new_uuid;
                                     } else {
                                       FX_CHECK(uuid1.value() == new_uuid);
                                     }
                                     ++num_uuid1;
                                   }));
  }
  RunLoopFor(kWindow);
  ASSERT_EQ(num_uuid1, kNumRequests);

  size_t num_uuid2{0};
  std::optional<std::string> uuid2{std::nullopt};
  for (size_t i = 0; i < kNumRequests; ++i) {
    ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                   ([&uuid2, &num_uuid2](const std::string& new_uuid) {
                                     if (!uuid2.has_value()) {
                                       uuid2 = new_uuid;
                                     } else {
                                       FX_CHECK(uuid2.value() == new_uuid);
                                     }
                                     ++num_uuid2;
                                   }));
  }
  RunLoopFor(kWindow);
  ASSERT_EQ(num_uuid2, kNumRequests);

  ASSERT_TRUE(uuid1.has_value());
  ASSERT_TRUE(uuid2.has_value());
  EXPECT_NE(uuid1.value(), uuid2.value());
}

TEST_F(SnapshotManagerTest, Check_Get) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid.has_value());
  auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
  ASSERT_TRUE(snapshot.LockAnnotations());
  ASSERT_TRUE(snapshot.LockArchive());

  EXPECT_THAT(snapshot.LockAnnotations()->Raw(), IsSupersetOf(Vector(kDefaultAnnotations)));
  EXPECT_EQ(snapshot.LockArchive()->key, kDefaultArchiveKey);
}

TEST_F(SnapshotManagerTest, Check_SetsDebugAnnotations) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid.has_value());
  auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
  ASSERT_TRUE(snapshot.LockAnnotations());
  ASSERT_TRUE(snapshot.LockArchive());

  std::map<std::string, std::string> annotations(kDefaultAnnotations.begin(),
                                                 kDefaultAnnotations.end());
  annotations["debug.snapshot.shared-request.num-clients"] = std::to_string(1);
  annotations["debug.snapshot.shared-request.uuid"] = uuid.value();

  EXPECT_THAT(snapshot.LockAnnotations()->Raw(), UnorderedElementsAreArray(Vector(annotations)));
}

TEST_F(SnapshotManagerTest, Check_AnnotationsMaxSizeIsEnforced) {
  SetUpDefaultDataProviderServer();

  // Initialize the manager to only hold the default annotations and the debug annotations.
  SetUpSnapshotManager(StorageSize::Bytes(256), StorageSize::Bytes(0));

  std::optional<std::string> uuid1{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid1](const std::string& new_uuid) { uuid1 = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid1.has_value());
  ASSERT_TRUE(snapshot_manager_->GetSnapshot(uuid1.value()).LockAnnotations());

  clock_.Set(clock_.Now() + kWindow);

  std::optional<std::string> uuid2{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid2](const std::string& new_uuid) { uuid2 = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid2.has_value());
  ASSERT_TRUE(snapshot_manager_->GetSnapshot(uuid2.value()).LockAnnotations());

  EXPECT_THAT(snapshot_manager_->GetSnapshot(uuid1.value()).LockAnnotations()->Raw(),
              UnorderedElementsAreArray({
                  Pair("debug.snapshot.error", "garbage collected"),
                  Pair("debug.snapshot.present", "false"),
              }));
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   uuid1.value(),
                                                   uuid2.value(),
                                               }));
}

TEST_F(SnapshotManagerTest, Check_Release) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid.has_value());
  {
    auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
    ASSERT_TRUE(snapshot.LockAnnotations());
    ASSERT_TRUE(snapshot.LockArchive());
  }

  snapshot_manager_->Release(uuid.value());
  {
    auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
    EXPECT_THAT(snapshot.LockAnnotations()->Raw(),
                UnorderedElementsAreArray({
                    Pair("debug.snapshot.error", "garbage collected"),
                    Pair("debug.snapshot.present", "false"),
                }));
    EXPECT_FALSE(snapshot.LockArchive());
  }
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   uuid.value(),
                                               }));
}

TEST_F(SnapshotManagerTest, Check_ArchivesMaxSizeIsEnforced) {
  SetUpDefaultDataProviderServer();

  // Initialize the manager to only hold a single default snapshot archive..
  SetUpSnapshotManager(StorageSize::Megabytes(1), StorageSize::Bytes(kDefaultArchiveKey.size()));

  std::optional<std::string> uuid1{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid1](const std::string& new_uuid) { uuid1 = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid1.has_value());
  ASSERT_TRUE(snapshot_manager_->GetSnapshot(uuid1.value()).LockArchive());

  clock_.Set(clock_.Now() + kWindow);

  std::optional<std::string> uuid2{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid2](const std::string& new_uuid) { uuid2 = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid2.has_value());
  ASSERT_TRUE(snapshot_manager_->GetSnapshot(uuid2.value()).LockArchive());

  EXPECT_FALSE(snapshot_manager_->GetSnapshot(uuid1.value()).LockArchive());
  EXPECT_THAT(snapshot_manager_->GetSnapshot(uuid1.value()).LockAnnotations()->Raw(),
              UnorderedElementsAreArray({
                  Pair("annotation.key.one", "annotation.value.one"),
                  Pair("annotation.key.two", "annotation.value.two"),
                  Pair("debug.snapshot.error", "garbage collected"),
                  Pair("debug.snapshot.present", "false"),
                  Pair("debug.snapshot.shared-request.num-clients", "1"),
                  Pair("debug.snapshot.shared-request.uuid", uuid1.value().c_str()),
              }));
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   uuid1.value(),
                                               }));
}

TEST_F(SnapshotManagerTest, Check_Timeout) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::sec(0),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid.has_value());
  auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
  EXPECT_THAT(snapshot.LockAnnotations()->Raw(), UnorderedElementsAreArray({
                                                     Pair("debug.snapshot.error", "timeout"),
                                                     Pair("debug.snapshot.present", "false"),
                                                 }));
  EXPECT_FALSE(snapshot.LockArchive());
}

TEST_F(SnapshotManagerTest, Check_UuidForNoSnapshotUuid) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();
  auto snapshot = snapshot_manager_->GetSnapshot(SnapshotManager::UuidForNoSnapshotUuid());
  EXPECT_THAT(snapshot.LockAnnotations()->Raw(), UnorderedElementsAreArray({
                                                     Pair("debug.snapshot.error", "missing uuid"),
                                                     Pair("debug.snapshot.present", "false"),
                                                 }));
  EXPECT_FALSE(snapshot.LockArchive());
}

TEST_F(SnapshotManagerTest, Check_Shutdown) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  snapshot_manager_->Shutdown();
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid.has_value());
  auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
  EXPECT_THAT(snapshot.LockAnnotations()->Raw(),
              IsSupersetOf({
                  Pair("debug.snapshot.error", "system shutdown"),
                  Pair("debug.snapshot.present", "false"),
              }));
  EXPECT_FALSE(snapshot.LockArchive());

  uuid = std::nullopt;
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid.has_value());
  snapshot = snapshot_manager_->GetSnapshot(uuid.value());
  EXPECT_THAT(snapshot.LockAnnotations()->Raw(),
              IsSupersetOf({
                  Pair("debug.snapshot.error", "system shutdown"),
                  Pair("debug.snapshot.present", "false"),
              }));
  EXPECT_FALSE(snapshot.LockArchive());
}

TEST_F(SnapshotManagerTest, Check_DefaultToNotPersisted) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::string uuid("UNKNOWN");
  auto snapshot = snapshot_manager_->GetSnapshot(uuid);
  EXPECT_THAT(snapshot.LockAnnotations()->Raw(), UnorderedElementsAreArray({
                                                     Pair("debug.snapshot.error", "not persisted"),
                                                     Pair("debug.snapshot.present", "false"),
                                                 }));
  EXPECT_FALSE(snapshot.LockArchive());
}

TEST_F(SnapshotManagerTest, Check_ReadPreviouslyGarbageCollected) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid.has_value());
  {
    auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
    ASSERT_TRUE(snapshot.LockAnnotations());
    ASSERT_TRUE(snapshot.LockArchive());
  }

  snapshot_manager_->Release(uuid.value());
  {
    auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
    EXPECT_THAT(snapshot.LockAnnotations()->Raw(),
                UnorderedElementsAreArray({
                    Pair("debug.snapshot.error", "garbage collected"),
                    Pair("debug.snapshot.present", "false"),
                }));
    EXPECT_FALSE(snapshot.LockArchive());
  }
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   uuid.value(),
                                               }));

  SetUpSnapshotManager(StorageSize::Megabytes(1u), StorageSize::Megabytes(1u));
  {
    auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
    EXPECT_THAT(snapshot.LockAnnotations()->Raw(),
                UnorderedElementsAreArray({
                    Pair("debug.snapshot.error", "garbage collected"),
                    Pair("debug.snapshot.present", "false"),
                }));
    EXPECT_FALSE(snapshot.LockArchive());
  }
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
