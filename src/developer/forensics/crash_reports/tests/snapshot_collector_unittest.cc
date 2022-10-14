// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_collector.h"

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

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/crash_reports/tests/scoped_test_report_store.h"
#include "src/developer/forensics/crash_reports/tests/stub_crash_server.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/data_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/clock.h"
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
constexpr zx::duration kUploadResponseDelay = zx::sec(0);
constexpr CrashServer::UploadStatus kUploadSuccessful = CrashServer::UploadStatus::kSuccess;

const std::map<std::string, std::string> kDefaultAnnotations = {
    {"annotation.key.one", "annotation.value.one"},
    {"annotation.key.two", "annotation.value.two"},
};

const std::string kDefaultArchiveKey = "snapshot.key";
constexpr char kProgramName[] = "crashing_program";

MissingSnapshot AsMissing(Snapshot snapshot) {
  FX_CHECK(std::holds_alternative<MissingSnapshot>(snapshot));
  return std::get<MissingSnapshot>(snapshot);
}

feedback::Annotations BuildFeedbackAnnotations(
    const std::map<std::string, std::string>& annotations) {
  feedback::Annotations ret_annotations;
  for (const auto& [key, value] : annotations) {
    ret_annotations.insert({key, value});
  }
  return ret_annotations;
}

class SnapshotCollectorTest : public UnitTestFixture {
 public:
  SnapshotCollectorTest()
      : UnitTestFixture(),
        clock_(),
        executor_(dispatcher()),
        snapshot_collector_(nullptr),
        path_(files::JoinPath(tmp_dir_.path(), "garbage_collected_snapshots.txt")) {
    report_store_ = std::make_unique<ScopedTestReportStore>(
        &annotation_manager_,
        std::make_shared<InfoContext>(&InspectRoot(), &clock_, dispatcher(), services()));
  }

  void SetUp() override {
    info_context_ =
        std::make_shared<InfoContext>(&InspectRoot(), &clock_, dispatcher(), services());
    SetUpQueue();
  }

 protected:
  void SetUpQueue(const std::vector<CrashServer::UploadStatus>& upload_attempt_results =
                      std::vector<CrashServer::UploadStatus>{}) {
    crash_server_ = std::make_unique<StubCrashServer>(dispatcher(), services(),
                                                      upload_attempt_results, kUploadResponseDelay);

    queue_ = std::make_unique<Queue>(dispatcher(), services(), info_context_, &tags_,
                                     &report_store_->GetReportStore(), crash_server_.get());
    queue_->WatchReportingPolicy(&reporting_policy_watcher_);
  }

  void SetUpDefaultSnapshotManager() {
    SetUpSnapshotManager(StorageSize::Megabytes(1u), StorageSize::Megabytes(1u));
  }

  void SetUpSnapshotManager(StorageSize max_annotations_size, StorageSize max_archives_size) {
    FX_CHECK(data_provider_server_);
    clock_.Set(zx::time(0u));
    snapshot_collector_ =
        std::make_unique<SnapshotCollector>(dispatcher(), &clock_, data_provider_server_.get(),
                                            GetSnapshotStore(), queue_.get(), kWindow);
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

  void ScheduleGetReportAndThen(const zx::duration timeout, ReportId report_id,
                                ::fit::function<void(Report&)> and_then) {
    timekeeper::time_utc utc_time;
    FX_CHECK(clock_.UtcNow(&utc_time) == ZX_OK);

    Product product{
        .name = "some name",
        .version = "some version",
        .channel = "some channel",
    };

    fuchsia::feedback::CrashReport report;
    report.set_program_name(kProgramName);

    executor_.schedule_task(snapshot_collector_
                                ->GetReport(timeout, std::move(report), report_id, utc_time,
                                            product, false, ReportingPolicy::kUpload)
                                .and_then(std::move(and_then))
                                .or_else([]() { FX_CHECK(false); }));
  }

  void CloseConnection() { data_provider_server_->CloseConnection(); }

  bool is_server_bound() { return data_provider_server_->IsBound(); }

  SnapshotStore* GetSnapshotStore() { return report_store_->GetReportStore().GetSnapshotStore(); }

  Snapshot GetSnapshot(const std::string& uuid) { return GetSnapshotStore()->GetSnapshot(uuid); }

  timekeeper::TestClock clock_;
  async::Executor executor_;
  std::unique_ptr<SnapshotCollector> snapshot_collector_;
  feedback::AnnotationManager annotation_manager_{dispatcher(), {}};
  std::unique_ptr<ScopedTestReportStore> report_store_;
  std::unique_ptr<Queue> queue_;

 private:
  std::unique_ptr<stubs::DataProviderBase> data_provider_server_;
  LogTags tags_;
  std::shared_ptr<InfoContext> info_context_;
  std::unique_ptr<StubCrashServer> crash_server_;
  StaticReportingPolicyWatcher<ReportingPolicy::kUpload> reporting_policy_watcher_;
  files::ScopedTempDir tmp_dir_;
  std::string path_;
};

TEST_F(SnapshotCollectorTest, Check_GetReport) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<Report> report{std::nullopt};
  ScheduleGetReportAndThen(zx::duration::infinite(), 0,
                           ([&report](Report& new_report) { report = std::move(new_report); }));

  // |report| should only have a value once |kWindow| has passed.
  RunLoopUntilIdle();
  ASSERT_FALSE(report.has_value());

  RunLoopFor(kWindow);
  ASSERT_TRUE(report.has_value());
}

TEST_F(SnapshotCollectorTest, Check_GetReportRequestsCombined) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  const size_t kNumRequests{5u};

  size_t num_snapshot_uuid1{0};
  std::optional<std::string> snapshot_uuid1{std::nullopt};
  for (size_t i = 0; i < kNumRequests; ++i) {
    ScheduleGetReportAndThen(zx::duration::infinite(), i,
                             ([&snapshot_uuid1, &num_snapshot_uuid1](Report& new_report) {
                               if (!snapshot_uuid1.has_value()) {
                                 snapshot_uuid1 = new_report.SnapshotUuid();
                               } else {
                                 FX_CHECK(snapshot_uuid1.value() == new_report.SnapshotUuid());
                               }
                               ++num_snapshot_uuid1;
                             }));
  }
  RunLoopFor(kWindow);
  ASSERT_EQ(num_snapshot_uuid1, kNumRequests);

  size_t num_snapshot_uuid2{0};
  std::optional<std::string> snapshot_uuid2{std::nullopt};
  for (size_t i = 0; i < kNumRequests; ++i) {
    ScheduleGetReportAndThen(zx::duration::infinite(), kNumRequests + i,
                             ([&snapshot_uuid2, &num_snapshot_uuid2](Report& new_report) {
                               if (!snapshot_uuid2.has_value()) {
                                 snapshot_uuid2 = new_report.SnapshotUuid();
                               } else {
                                 FX_CHECK(snapshot_uuid2.value() == new_report.SnapshotUuid());
                               }
                               ++num_snapshot_uuid2;
                             }));
  }
  RunLoopFor(kWindow);
  ASSERT_EQ(num_snapshot_uuid2, kNumRequests);

  ASSERT_TRUE(snapshot_uuid1.has_value());
  ASSERT_TRUE(snapshot_uuid2.has_value());
  EXPECT_NE(snapshot_uuid1.value(), snapshot_uuid2.value());
}

TEST_F(SnapshotCollectorTest, Check_MultipleSimultaneousRequests) {
  // Setup report store to not have room for more than 1 report.
  report_store_ = std::make_unique<ScopedTestReportStore>(
      &annotation_manager_,
      std::make_shared<InfoContext>(&InspectRoot(), &clock_, dispatcher(), services()),
      StorageSize::Bytes(1));

  auto data_provider_owner =
      std::make_unique<stubs::DataProviderReturnsOnDemand>(kDefaultAnnotations, kDefaultArchiveKey);
  auto data_provider = data_provider_owner.get();

  SetUpDataProviderServer(std::move(data_provider_owner));
  SetUpDefaultSnapshotManager();

  std::optional<Report> report1{std::nullopt};
  ScheduleGetReportAndThen(zx::duration::infinite(), 1,
                           ([&report1](Report& new_report) { report1 = std::move(new_report); }));

  RunLoopFor(kWindow);

  std::optional<Report> report2{std::nullopt};
  ScheduleGetReportAndThen(zx::duration::infinite(), 2,
                           ([&report2](Report& new_report) { report2 = std::move(new_report); }));
  RunLoopFor(kWindow);

  // |report1| should only have a value once snapshot generation is complete.
  RunLoopUntilIdle();
  ASSERT_FALSE(report1.has_value());
  data_provider->PopSnapshotInternalCallback();

  RunLoopUntilIdle();
  ASSERT_TRUE(report1.has_value());

  // |report2| should only have a value once snapshot generation is complete.
  RunLoopUntilIdle();
  ASSERT_FALSE(report2.has_value());

  data_provider->PopSnapshotInternalCallback();

  RunLoopUntilIdle();
  ASSERT_TRUE(report2.has_value());
}

TEST_F(SnapshotCollectorTest, Check_Timeout) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<Report> report{std::nullopt};
  ScheduleGetReportAndThen(zx::sec(0), 0,
                           ([&report](Report& new_report) { report = std::move(new_report); }));

  // TODO(fxbug.dev/111793): Check annotations to get intended snapshot uuid. Delete unnecessary
  // SnapshotStore::Size function.
  ASSERT_EQ(GetSnapshotStore()->Size(), 0u);

  RunLoopFor(kWindow);

  ASSERT_TRUE(report.has_value());
  auto snapshot = AsMissing(GetSnapshot(report->SnapshotUuid()));
  EXPECT_THAT(snapshot.PresenceAnnotations(), UnorderedElementsAreArray({
                                                  Pair("debug.snapshot.error", "timeout"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
  EXPECT_EQ(GetSnapshotStore()->Size(), 0u);
}

TEST_F(SnapshotCollectorTest, Check_Shutdown) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<Report> report{std::nullopt};
  ScheduleGetReportAndThen(zx::duration::infinite(), 0,
                           ([&report](Report& new_report) { report = std::move(new_report); }));
  snapshot_collector_->Shutdown();
  RunLoopUntilIdle();

  ASSERT_TRUE(report.has_value());
  auto snapshot = AsMissing(GetSnapshot(report->SnapshotUuid()));
  EXPECT_THAT(snapshot.PresenceAnnotations(), IsSupersetOf({
                                                  Pair("debug.snapshot.error", "system shutdown"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));

  report = std::nullopt;
  ScheduleGetReportAndThen(zx::duration::infinite(), 1,
                           ([&report](Report& new_report) { report = std::move(new_report); }));
  RunLoopUntilIdle();

  ASSERT_TRUE(report.has_value());
  snapshot = AsMissing(GetSnapshot(report->SnapshotUuid()));
  EXPECT_THAT(snapshot.PresenceAnnotations(), IsSupersetOf({
                                                  Pair("debug.snapshot.error", "system shutdown"),
                                                  Pair("debug.snapshot.present", "false"),
                                              }));
}

TEST_F(SnapshotCollectorTest, Check_SetsPresenceAnnotations) {
  SetUpDefaultDataProviderServer();
  SetUpDefaultSnapshotManager();

  std::optional<Report> report{std::nullopt};
  ScheduleGetReportAndThen(zx::duration::infinite(), 0,
                           ([&report](Report& new_report) { report = std::move(new_report); }));

  RunLoopFor(kWindow);
  ASSERT_TRUE(report.has_value());

  EXPECT_THAT(BuildFeedbackAnnotations(report->Annotations().Raw()),
              IsSupersetOf({
                  Pair("debug.snapshot.shared-request.num-clients", std::to_string(1)),
                  Pair("debug.snapshot.shared-request.uuid", report->SnapshotUuid()),
              }));
}

TEST_F(SnapshotCollectorTest, Check_ClientsAddedToQueue) {
  SetUpDefaultDataProviderServer();
  SetUpQueue({
      kUploadSuccessful,
      kUploadSuccessful,
  });
  SetUpDefaultSnapshotManager();

  // Generate 2 reports sharing the same snapshot.
  std::optional<Report> report1{std::nullopt};
  ScheduleGetReportAndThen(zx::duration::infinite(), 0,
                           ([&report1](Report& new_report) { report1 = std::move(new_report); }));

  std::optional<Report> report2{std::nullopt};
  ScheduleGetReportAndThen(zx::duration::infinite(), 1,
                           ([&report2](Report& new_report) { report2 = std::move(new_report); }));

  RunLoopFor(kWindow);
  ASSERT_TRUE(report1.has_value());
  ASSERT_TRUE(report2.has_value());
  ASSERT_EQ(report1->SnapshotUuid(), report2->SnapshotUuid());

  // Add to queue to ensure we don't delete the snapshot prematurely after upload of the first
  // report.
  ASSERT_TRUE(GetSnapshotStore()->SnapshotExists(report1->SnapshotUuid()));
  queue_->Add(std::move(*report1));

  // Run loop until idle so Queue will finish "upload".
  RunLoopUntilIdle();

  const SnapshotUuid uuid2 = report2->SnapshotUuid();
  ASSERT_TRUE(GetSnapshotStore()->SnapshotExists(uuid2));
  queue_->Add(std::move(*report2));

  // Run loop until idle so Queue will finish "upload".
  RunLoopUntilIdle();

  EXPECT_FALSE(GetSnapshotStore()->SnapshotExists(uuid2));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
