// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_manager.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/errors.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/data_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace crash_reports {
namespace {

using testing::Contains;
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
      : UnitTestFixture(), clock_(nullptr), executor_(dispatcher()), snapshot_manager_(nullptr) {}

 protected:
  void SetUp() override {
    SetUpSnapshotManager(StorageSize::Megabytes(1u), StorageSize::Megabytes(1u));
  }

  void SetUpSnapshotManager(StorageSize max_annotations_size, StorageSize max_archives_size) {
    clock_ = new timekeeper::TestClock();
    snapshot_manager_ = std::make_unique<SnapshotManager>(
        dispatcher(), services(), std::unique_ptr<timekeeper::TestClock>(clock_), kWindow,
        max_annotations_size, max_archives_size);
  }

  void SetUpDefaultDataProviderServer() {
    SetUpDataProviderServer(
        std::make_unique<stubs::DataProvider>(kDefaultAnnotations, kDefaultArchiveKey));
  }

  void SetUpDataProviderServer(std::unique_ptr<stubs::DataProviderBase> data_provider_server) {
    data_provider_server_ = std::move(data_provider_server);
    if (data_provider_server_) {
      InjectServiceProvider(data_provider_server_.get());
    }
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

  timekeeper::TestClock* clock_;
  async::Executor executor_;
  std::unique_ptr<SnapshotManager> snapshot_manager_;

 private:
  std::unique_ptr<stubs::DataProviderBase> data_provider_server_;
};

TEST_F(SnapshotManagerTest, Check_GetSnapshotUuid) {
  SetUpDefaultDataProviderServer();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid.has_value());
}

TEST_F(SnapshotManagerTest, Check_GetSnapshotUuidRequestsCombined) {
  SetUpDefaultDataProviderServer();

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
  clock_->Set(clock_->Now() + kWindow);

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
  RunLoopUntilIdle();

  ASSERT_EQ(num_uuid1, kNumRequests);
  ASSERT_EQ(num_uuid2, kNumRequests);

  ASSERT_TRUE(uuid1.has_value());
  ASSERT_TRUE(uuid2.has_value());
  EXPECT_NE(uuid1.value(), uuid2.value());
}

TEST_F(SnapshotManagerTest, Check_Get) {
  SetUpDefaultDataProviderServer();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid.has_value());
  auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
  ASSERT_TRUE(snapshot.LockAnnotations());
  ASSERT_TRUE(snapshot.LockArchive());

  EXPECT_THAT(*(snapshot.LockAnnotations()), IsSupersetOf(Vector(kDefaultAnnotations)));
  EXPECT_EQ(snapshot.LockArchive()->key, kDefaultArchiveKey);
}

TEST_F(SnapshotManagerTest, Check_SetsDebugAnnotations) {
  SetUpDefaultDataProviderServer();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid.has_value());
  auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
  ASSERT_TRUE(snapshot.LockAnnotations());
  ASSERT_TRUE(snapshot.LockArchive());

  std::map<std::string, std::string> annotations(kDefaultAnnotations.begin(),
                                                 kDefaultAnnotations.end());
  annotations["debug.snapshot.shared-request.num-clients"] = std::to_string(1);
  annotations["debug.snapshot.shared-request.uuid"] = uuid.value();

  EXPECT_THAT(*(snapshot.LockAnnotations()), UnorderedElementsAreArray(Vector(annotations)));
}

TEST_F(SnapshotManagerTest, Check_ConnectionFailure) {
  const size_t kNumRequests{5u};
  std::vector<std::string> uuids;
  for (size_t i = 0; i < kNumRequests; ++i) {
    ScheduleGetSnapshotUuidAndThen(
        zx::duration::infinite(),
        ([&uuids](const std::string& new_uuid) { uuids.push_back(new_uuid); }));
    clock_->Set(clock_->Now() + kWindow);
  }
  RunLoopUntilIdle();

  ASSERT_EQ(uuids.size(), kNumRequests);
  for (const auto& uuid : uuids) {
    auto snapshot = snapshot_manager_->GetSnapshot(uuid);
    ASSERT_TRUE(snapshot.LockAnnotations());
    EXPECT_THAT(*(snapshot.LockAnnotations()),
                Contains(Pair("debug.snapshot.error", ToReason(Error::kConnectionError))));
    EXPECT_FALSE(snapshot.LockArchive());
  }
}

TEST_F(SnapshotManagerTest, Check_AnnotationsMaxSizeIsEnforced) {
  SetUpDefaultDataProviderServer();

  // Initialize the manager to only hold the default annotations and the debug annotations.
  SetUpSnapshotManager(StorageSize::Bytes(256), StorageSize::Bytes(0));

  std::optional<std::string> uuid1{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid1](const std::string& new_uuid) { uuid1 = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid1.has_value());
  ASSERT_TRUE(snapshot_manager_->GetSnapshot(uuid1.value()).LockAnnotations());

  clock_->Set(clock_->Now() + kWindow);

  std::optional<std::string> uuid2{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid2](const std::string& new_uuid) { uuid2 = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid2.has_value());
  ASSERT_TRUE(snapshot_manager_->GetSnapshot(uuid2.value()).LockAnnotations());

  EXPECT_THAT(*(snapshot_manager_->GetSnapshot(uuid1.value()).LockAnnotations()),
              UnorderedElementsAreArray({Pair("debug.snapshot.error", "garbage collected")}));
}

TEST_F(SnapshotManagerTest, Check_Release) {
  SetUpDefaultDataProviderServer();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid.has_value());
  {
    auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
    ASSERT_TRUE(snapshot.LockAnnotations());
    ASSERT_TRUE(snapshot.LockArchive());
  }

  snapshot_manager_->Release(uuid.value());
  {
    auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
    EXPECT_THAT(*(snapshot_manager_->GetSnapshot(uuid.value()).LockAnnotations()),
                UnorderedElementsAreArray({Pair("debug.snapshot.error", "garbage collected")}));
    EXPECT_FALSE(snapshot.LockArchive());
  }
}

TEST_F(SnapshotManagerTest, Check_ArchivesMaxSizeIsEnforced) {
  SetUpDefaultDataProviderServer();

  // Initialize the manager to only hold a single default snapshot archive..
  SetUpSnapshotManager(StorageSize::Bytes(0), StorageSize::Bytes(kDefaultArchiveKey.size()));

  std::optional<std::string> uuid1{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid1](const std::string& new_uuid) { uuid1 = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid1.has_value());
  ASSERT_TRUE(snapshot_manager_->GetSnapshot(uuid1.value()).LockArchive());

  clock_->Set(clock_->Now() + kWindow);

  std::optional<std::string> uuid2{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::duration::infinite(),
                                 ([&uuid2](const std::string& new_uuid) { uuid2 = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid2.has_value());
  ASSERT_TRUE(snapshot_manager_->GetSnapshot(uuid2.value()).LockArchive());

  EXPECT_FALSE(snapshot_manager_->GetSnapshot(uuid1.value()).LockArchive());
}

TEST_F(SnapshotManagerTest, Check_Timeout) {
  SetUpDefaultDataProviderServer();

  std::optional<std::string> uuid{std::nullopt};
  ScheduleGetSnapshotUuidAndThen(zx::sec(0),
                                 ([&uuid](const std::string& new_uuid) { uuid = new_uuid; }));
  RunLoopUntilIdle();

  ASSERT_TRUE(uuid.has_value());
  auto snapshot = snapshot_manager_->GetSnapshot(uuid.value());
  EXPECT_THAT(*(snapshot_manager_->GetSnapshot(uuid.value()).LockAnnotations()),
              UnorderedElementsAreArray({Pair("debug.snapshot.error", "timeout")}));
  EXPECT_FALSE(snapshot.LockArchive());
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
