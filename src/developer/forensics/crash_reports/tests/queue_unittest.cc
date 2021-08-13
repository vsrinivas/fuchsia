// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/queue.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/network_watcher.h"
#include "src/developer/forensics/crash_reports/reporting_policy_watcher.h"
#include "src/developer/forensics/crash_reports/tests/stub_crash_server.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/network_reachability_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace crash_reports {
namespace {

using testing::Contains;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::IsSupersetOf;
using testing::Not;
using testing::Pair;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

constexpr CrashServer::UploadStatus kUploadSuccessful = CrashServer::UploadStatus::kSuccess;
constexpr CrashServer::UploadStatus kUploadFailed = CrashServer::UploadStatus::kFailure;
constexpr CrashServer::UploadStatus kUploadThrottled = CrashServer::UploadStatus::kThrottled;
constexpr CrashServer::UploadStatus kUploadTimedOut = CrashServer::UploadStatus::kTimedOut;

constexpr char kAttachmentKey[] = "attachment.key";
constexpr char kAttachmentValue[] = "attachment.value";
constexpr char kAnnotationKey[] = "annotation.key";
constexpr char kAnnotationValue[] = "annotation.value";
constexpr char kSnapshotUuidValue[] = "snapshot_uuid";
constexpr char kMinidumpKey[] = "uploadFileMinidump";
constexpr char kMinidumpValue[] = "minidump";

constexpr zx::duration kPeriodicUploadDuration = zx::min(15);

fuchsia::mem::Buffer BuildAttachment(const std::string& value) {
  fuchsia::mem::Buffer attachment;
  FX_CHECK(fsl::VmoFromString(value, &attachment));
  return attachment;
}

std::map<std::string, fuchsia::mem::Buffer> MakeAttachments() {
  std::map<std::string, fuchsia::mem::Buffer> attachments;
  attachments[kAttachmentKey] = BuildAttachment(kAttachmentValue);
  return attachments;
}

std::optional<std::string> DeleteReportFromStore() {
  auto RemoveCurDir = [](std::vector<std::string>* contents) {
    contents->erase(std::remove(contents->begin(), contents->end(), "."), contents->end());
  };

  std::vector<std::string> program_shortnames;
  files::ReadDirContents(kStoreCachePath, &program_shortnames);
  RemoveCurDir(&program_shortnames);
  for (const auto& program_shortname : program_shortnames) {
    const std::string path = files::JoinPath(kStoreCachePath, program_shortname);

    std::vector<std::string> report_ids;
    files::ReadDirContents(path, &report_ids);
    RemoveCurDir(&report_ids);

    if (!report_ids.empty()) {
      files::DeletePath(files::JoinPath(path, report_ids.back()), /*recursive=*/true);
      return report_ids.back();
    }
  }
  return std::nullopt;
}

AnnotationMap MakeAnnotations() { return {{kAnnotationKey, kAnnotationValue}}; }

Report MakeReport(const std::size_t report_id) {
  std::optional<Report> report =
      Report::MakeReport(report_id, fxl::StringPrintf("program_%ld", report_id), MakeAnnotations(),
                         MakeAttachments(), kSnapshotUuidValue, BuildAttachment(kMinidumpValue));
  FX_CHECK(report.has_value());
  return std::move(report.value());
}

Report MakeHourlyReport(const std::size_t report_id) {
  std::optional<Report> report = Report::MakeReport(
      report_id, kHourlySnapshotProgramName, MakeAnnotations(), MakeAttachments(),
      kSnapshotUuidValue, BuildAttachment(kMinidumpValue), /*is_hourly_report=*/true);
  FX_CHECK(report.has_value());
  return std::move(report.value());
}

class TestReportingPolicyWatcher : public ReportingPolicyWatcher {
 public:
  TestReportingPolicyWatcher() : ReportingPolicyWatcher(ReportingPolicy::kUndecided) {}

  void Set(const ReportingPolicy policy) { SetPolicy(policy); }
};

class QueueTest : public UnitTestFixture {
 public:
  QueueTest() : network_watcher_(dispatcher(), *services()){};

  void SetUp() override {
    info_context_ =
        std::make_shared<InfoContext>(&InspectRoot(), &clock_, dispatcher(), services());

    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    SetUpNetworkReachabilityProvider();
    RunLoopUntilIdle();
  }

  void TearDown() override {
    ASSERT_TRUE(files::DeletePath(kStoreTmpPath, /*recursive=*/true));
    ASSERT_TRUE(files::DeletePath(kStoreCachePath, /*recursive=*/true));
  }

 protected:
  void SetUpNetworkReachabilityProvider() {
    network_reachability_provider_ = std::make_unique<stubs::NetworkReachabilityProvider>();
    InjectServiceProvider(network_reachability_provider_.get());
  }

  void SetUpQueue(const std::vector<CrashServer::UploadStatus>& upload_attempt_results =
                      std::vector<CrashServer::UploadStatus>{}) {
    report_id_ = 1;
    snapshot_manager_ = std::make_unique<SnapshotManager>(
        dispatcher(), services(), &clock_, zx::sec(5), kGarbageCollectedSnapshotsPath,
        StorageSize::Gigabytes(1), StorageSize::Gigabytes(1));
    crash_server_ = std::make_unique<StubCrashServer>(upload_attempt_results);

    InitQueue();
  }

  void InitQueue() {
    queue_ = std::make_unique<Queue>(dispatcher(), services(), info_context_, &tags_,
                                     crash_server_.get(), snapshot_manager_.get());
    queue_->WatchReportingPolicy(&reporting_policy_watcher_);
    queue_->WatchNetwork(&network_watcher_);
  }

  std::optional<ReportId> AddNewReport(const bool is_hourly_report) {
    ++report_id_;
    Report report = (is_hourly_report) ? MakeHourlyReport(report_id_) : MakeReport(report_id_);

    if (queue_->Add(std::move(report))) {
      return report_id_;
    }
    return std::nullopt;
  }

  void CheckAnnotationsOnServer() {
    FX_CHECK(crash_server_);

    // Expect annotations that |snapshot_manager_| will for using |kSnapshotUuidValue| as the
    // snapshot uuid.
    EXPECT_THAT(crash_server_->latest_annotations().Raw(),
                UnorderedElementsAreArray({
                    Pair(kAnnotationKey, kAnnotationValue),
                    Pair("debug.snapshot.error", "not persisted"),
                    Pair("debug.snapshot.present", "false"),
                }));
  }

  void CheckAttachmentKeysOnServer() {
    FX_CHECK(crash_server_);
    EXPECT_THAT(crash_server_->latest_attachment_keys(),
                UnorderedElementsAre(kAttachmentKey, kMinidumpKey));
  }

  LogTags tags_;
  std::unique_ptr<Queue> queue_;
  TestReportingPolicyWatcher reporting_policy_watcher_;

  size_t report_id_ = 1;

  NetworkWatcher network_watcher_;
  timekeeper::TestClock clock_;
  std::unique_ptr<stubs::NetworkReachabilityProvider> network_reachability_provider_;
  std::unique_ptr<SnapshotManager> snapshot_manager_;
  std::unique_ptr<StubCrashServer> crash_server_;
  std::shared_ptr<InfoContext> info_context_;
  std::shared_ptr<cobalt::Logger> cobalt_;
};

TEST_F(QueueTest, Add_ReportingPolicyUndecided) {
  SetUpQueue();

  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);
  const auto report_id = AddNewReport(/*is_hourly_report=*/false);

  ASSERT_TRUE(*report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(QueueTest, Add_ReportingPolicyUndecided_HourlyReport) {
  SetUpQueue();

  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);
  const auto report_id_1 = AddNewReport(/*is_hourly_report=*/true);

  ASSERT_TRUE(*report_id_1);
  EXPECT_TRUE(queue_->Contains(*report_id_1));

  // Later hourly reports shouldn't be kept in the queue.
  const auto report_id_2 = AddNewReport(/*is_hourly_report=*/true);

  ASSERT_TRUE(*report_id_2);

  EXPECT_TRUE(queue_->Contains(*report_id_1));
  EXPECT_FALSE(queue_->Contains(*report_id_2));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::CrashState::kDeleted),
                                      }));
}

TEST_F(QueueTest, Add_ReportingPolicyDoNotFileAndDelete) {
  SetUpQueue();

  reporting_policy_watcher_.Set(ReportingPolicy::kDoNotFileAndDelete);
  const auto report_id = AddNewReport(/*is_hourly_report=*/false);

  ASSERT_TRUE(*report_id);
  EXPECT_FALSE(queue_->Contains(*report_id));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::CrashState::kDeleted),
                                      }));
}

TEST_F(QueueTest, Add_ReportingPolicyArchive) {
  SetUpQueue();

  reporting_policy_watcher_.Set(ReportingPolicy::kArchive);
  auto report_id = AddNewReport(/*is_hourly_report=*/false);

  ASSERT_TRUE(*report_id);
  EXPECT_FALSE(queue_->Contains(*report_id));

  report_id = AddNewReport(/*is_hourly_report=*/true);

  ASSERT_TRUE(*report_id);
  EXPECT_FALSE(queue_->Contains(*report_id));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::CrashState::kArchived),
                                          cobalt::Event(cobalt::CrashState::kArchived),
                                      }));
}

TEST_F(QueueTest, Add_ReportingPolicyUpload_Successful) {
  SetUpQueue({kUploadSuccessful});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  const auto report_id = AddNewReport(/*is_hourly_report=*/false);

  ASSERT_TRUE(*report_id);
  EXPECT_FALSE(queue_->Contains(*report_id));

  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
              }));
}

TEST_F(QueueTest, Add_ReportingPolicyUpload_Failure) {
  SetUpQueue({kUploadFailed});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  const auto report_id = AddNewReport(/*is_hourly_report=*/false);

  ASSERT_TRUE(*report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
              }));
}

TEST_F(QueueTest, Add_ReportingPolicyUpload_AfterStopUploading) {
  SetUpQueue();

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  queue_->StopUploading();

  const auto report_id = AddNewReport(/*is_hourly_report=*/false);

  ASSERT_TRUE(*report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(QueueTest, PeriodicUpload) {
  SetUpQueue({
      kUploadSuccessful,
      kUploadFailed,
      kUploadFailed,
      kUploadSuccessful,
      kUploadSuccessful,
  });
  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);

  auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  ASSERT_TRUE(queue_->IsPeriodicUploadScheduled());
  RunLoopUntilIdle();

  EXPECT_FALSE(queue_->Contains(*report_id));

  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
              }));

  report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  const auto hourly_report_id = AddNewReport(/*is_hourly_report=*/true);
  ASSERT_TRUE(hourly_report_id);
  EXPECT_TRUE(queue_->Contains(*hourly_report_id));

  RunLoopFor(kPeriodicUploadDuration);
  EXPECT_FALSE(queue_->Contains(*report_id));
  EXPECT_FALSE(queue_->Contains(*hourly_report_id));

  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 2u),
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 2u),
              }));
}

TEST_F(QueueTest, PeriodicUpload_ReportingPolicyChanges) {
  SetUpQueue();

  EXPECT_FALSE(queue_->IsPeriodicUploadScheduled());

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  EXPECT_TRUE(queue_->IsPeriodicUploadScheduled());

  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);
  EXPECT_FALSE(queue_->IsPeriodicUploadScheduled());

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  EXPECT_TRUE(queue_->IsPeriodicUploadScheduled());

  reporting_policy_watcher_.Set(ReportingPolicy::kDoNotFileAndDelete);
  EXPECT_FALSE(queue_->IsPeriodicUploadScheduled());

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  EXPECT_TRUE(queue_->IsPeriodicUploadScheduled());

  reporting_policy_watcher_.Set(ReportingPolicy::kArchive);
  EXPECT_TRUE(queue_->IsPeriodicUploadScheduled());
}

TEST_F(QueueTest, PeriodicUpload_AfterStopUploading) {
  SetUpQueue();

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  ASSERT_TRUE(queue_->IsPeriodicUploadScheduled());

  queue_->StopUploading();
  EXPECT_FALSE(queue_->IsPeriodicUploadScheduled());
}

TEST_F(QueueTest, UploadOnNetworkReachable) {
  SetUpQueue({kUploadFailed, kUploadFailed, kUploadSuccessful, kUploadSuccessful});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  const auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);

  const auto hourly_report_id = AddNewReport(/*is_hourly_report=*/true);
  ASSERT_TRUE(hourly_report_id);

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();

  EXPECT_FALSE(queue_->Contains(*report_id));
  EXPECT_FALSE(queue_->Contains(*hourly_report_id));

  RunLoopUntilIdle();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 2u),
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 2u),
              }));
}

TEST_F(QueueTest, UploadThrottled) {
  SetUpQueue({kUploadThrottled, kUploadFailed, kUploadThrottled});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_FALSE(queue_->Contains(*report_id));

  report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  RunLoopFor(kPeriodicUploadDuration);
  EXPECT_FALSE(queue_->Contains(*report_id));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploadThrottled),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadThrottled, 1u),
                  cobalt::Event(cobalt::CrashState::kUploadThrottled),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadThrottled, 2u),
              }));
}

TEST_F(QueueTest, UploadTimedOut) {
  SetUpQueue({kUploadTimedOut, kUploadFailed, kUploadTimedOut});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_FALSE(queue_->Contains(*report_id));

  report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  RunLoopFor(kPeriodicUploadDuration);
  EXPECT_FALSE(queue_->Contains(*report_id));

  RunLoopFor(kPeriodicUploadDuration);
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploadTimedOut),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadTimedOut, 1u),
                  cobalt::Event(cobalt::CrashState::kUploadTimedOut),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadTimedOut, 2u),
              }));
}

TEST_F(QueueTest, InitializeFromStore) {
  // This test cannot call RunLoopUntilIdle in any capacity once InitQueue has been called been
  // called for the second time. The watchers still hold callbacks tied to the old, deleted queue
  // and will crash if they attempt to execute the callbacks.
  SetUpQueue();
  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);

  const auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  SetUpQueue();
  EXPECT_TRUE(queue_->Contains(*report_id));
}

TEST_F(QueueTest, ReportDeletedByStore) {
  SetUpQueue();
  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);

  const auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  ASSERT_TRUE(DeleteReportFromStore().has_value());
  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  RunLoopFor(kPeriodicUploadDuration);

  EXPECT_FALSE(queue_->Contains(*report_id));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
