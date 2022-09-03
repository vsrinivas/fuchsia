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
#include <variant>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
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

ManagedSnapshot AsManaged(Snapshot snapshot) {
  FX_CHECK(std::holds_alternative<ManagedSnapshot>(snapshot));
  return std::get<ManagedSnapshot>(snapshot);
}

MissingSnapshot AsMissing(Snapshot snapshot) {
  FX_CHECK(std::holds_alternative<MissingSnapshot>(snapshot));
  return std::get<MissingSnapshot>(snapshot);
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
    snapshot_manager_ = std::make_unique<SnapshotManager>(
        dispatcher(), &clock_, data_provider_server_.get(), &annotation_manager_, kWindow, path_,
        max_annotations_size, max_archives_size);
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
  feedback::AnnotationManager annotation_manager_{dispatcher(), {}};
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

TEST_F(SnapshotManagerTest, Check_Release) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopFor(kWindow);

  ASSERT_TRUE(uuid.has_value());
  {
    auto snapshot = AsManaged(snapshot_manager_->GetSnapshot(uuid.value()));
    ASSERT_TRUE(snapshot.LockArchive());
  }

  snapshot_manager_->Release(uuid.value());
  {
    auto snapshot = AsMissing(snapshot_manager_->GetSnapshot(uuid.value()));
    EXPECT_THAT(snapshot.PresenceAnnotations(),
                UnorderedElementsAreArray({
                    Pair("debug.snapshot.error", "garbage collected"),
                    Pair("debug.snapshot.present", "false"),
                }));
  }
  EXPECT_THAT(ReadGarbageCollectedSnapshots(), UnorderedElementsAreArray({
                                                   uuid.value(),
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
  auto snapshot = AsMissing(snapshot_manager_->GetSnapshot(uuid.value()));
  EXPECT_THAT(snapshot.PresenceAnnotations(), UnorderedElementsAreArray({
                                                  Pair("debug.snapshot.error", "timeout"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
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
  auto snapshot = AsMissing(snapshot_manager_->GetSnapshot(uuid.value()));
  EXPECT_THAT(snapshot.PresenceAnnotations(), IsSupersetOf({
                                                  Pair("debug.snapshot.error", "system shutdown"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));

  uuid = std::nullopt;
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid.has_value());
  snapshot = AsMissing(snapshot_manager_->GetSnapshot(uuid.value()));
  EXPECT_THAT(snapshot.PresenceAnnotations(), IsSupersetOf({
                                                  Pair("debug.snapshot.error", "system shutdown"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
