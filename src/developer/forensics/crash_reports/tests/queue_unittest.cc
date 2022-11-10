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
#include "src/developer/forensics/crash_reports/tests/scoped_test_report_store.h"
#include "src/developer/forensics/crash_reports/tests/stub_crash_server.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/network_reachability_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/developer/forensics/utils/storage_size.h"
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
constexpr zx::duration kUploadResponseDelay = zx::sec(5);

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

AnnotationMap MakeAnnotations() { return {{kAnnotationKey, kAnnotationValue}}; }

Report MakeReport(const std::size_t report_id, const bool empty_annotations = false) {
  AnnotationMap annotations;
  if (!empty_annotations) {
    annotations = MakeAnnotations();
  }
  fpromise::result<Report> report =
      Report::MakeReport(report_id, fxl::StringPrintf("program_%ld", report_id), annotations,
                         MakeAttachments(), kSnapshotUuidValue, BuildAttachment(kMinidumpValue));
  FX_CHECK(report.is_ok());
  return std::move(report.value());
}

Report MakeHourlyReport(const std::size_t report_id, const bool empty_annotations = false) {
  AnnotationMap annotations;
  if (!empty_annotations) {
    annotations = MakeAnnotations();
  }
  fpromise::result<Report> report = Report::MakeReport(
      report_id, kHourlySnapshotProgramName, annotations, MakeAttachments(), kSnapshotUuidValue,
      BuildAttachment(kMinidumpValue), /*is_hourly_report=*/true);
  FX_CHECK(report.is_ok());
  return std::move(report.value());
}

class TestReportingPolicyWatcher : public ReportingPolicyWatcher {
 public:
  TestReportingPolicyWatcher() : ReportingPolicyWatcher(ReportingPolicy::kUndecided) {}

  void Set(const ReportingPolicy policy) { SetPolicy(policy); }
};

class QueueTest : public UnitTestFixture {
 public:
  QueueTest()
      : network_watcher_(dispatcher(), *services()), annotation_manager_(dispatcher(), {}) {}

  void SetUp() override {
    info_context_ =
        std::make_shared<InfoContext>(&InspectRoot(), &clock_, dispatcher(), services());
    report_store_ = std::make_unique<ScopedTestReportStore>(&annotation_manager_, info_context_);

    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    SetUpNetworkReachabilityProvider();
    RunLoopUntilIdle();
  }

 protected:
  void SetUpNetworkReachabilityProvider() {
    network_reachability_provider_ = std::make_unique<stubs::NetworkReachabilityProvider>();
    InjectServiceProvider(network_reachability_provider_.get());
  }

  void SetUpQueue(const std::vector<CrashServer::UploadStatus>& upload_attempt_results =
                      std::vector<CrashServer::UploadStatus>{}) {
    report_id_ = 1;
    crash_server_ = std::make_unique<StubCrashServer>(dispatcher(), services(),
                                                      upload_attempt_results, kUploadResponseDelay);

    InitQueue();
  }

  void InitQueue() {
    queue_ = std::make_unique<Queue>(dispatcher(), services(), info_context_, &tags_,
                                     &report_store_->GetReportStore(), crash_server_.get());
    queue_->WatchReportingPolicy(&reporting_policy_watcher_);
    queue_->WatchNetwork(&network_watcher_);
  }

  SnapshotStore* GetSnapshotStore() { return report_store_->GetReportStore().GetSnapshotStore(); }

  std::optional<ReportId> AddNewReport(const bool is_hourly_report,
                                       const bool empty_annotations = false) {
    ++report_id_;
    queue_->AddReportUsingSnapshot(kSnapshotUuidValue, report_id_);
    Report report = (is_hourly_report) ? MakeHourlyReport(report_id_, empty_annotations)
                                       : MakeReport(report_id_, empty_annotations);

    if (queue_->Add(std::move(report))) {
      return report_id_;
    }
    return std::nullopt;
  }

  void CheckAnnotationsOnServer() {
    FX_CHECK(crash_server_);

    // Expect annotations that |snapshot_collector_| will for using |kSnapshotUuidValue| as the
    // snapshot uuid.
    EXPECT_THAT(crash_server_->latest_annotations().Raw(),
                UnorderedElementsAreArray({
                    Pair(kAnnotationKey, kAnnotationValue),
                    Pair(feedback::kDebugSnapshotErrorKey, "not persisted"),
                    Pair(feedback::kDebugSnapshotPresentKey, "false"),
                }));
  }

  void CheckAttachmentKeysOnServer() {
    FX_CHECK(crash_server_);
    EXPECT_THAT(crash_server_->latest_attachment_keys(),
                UnorderedElementsAre(kAttachmentKey, kMinidumpKey));
  }

  std::optional<std::string> DeleteReportFromStore() {
    auto RemoveCurDir = [](std::vector<std::string>* contents) {
      contents->erase(std::remove(contents->begin(), contents->end(), "."), contents->end());
    };

    std::vector<std::string> program_shortnames;
    files::ReadDirContents(report_store_->GetCacheReportsPath(), &program_shortnames);
    RemoveCurDir(&program_shortnames);
    for (const auto& program_shortname : program_shortnames) {
      const std::string path =
          files::JoinPath(report_store_->GetCacheReportsPath(), program_shortname);

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

  LogTags tags_;
  std::unique_ptr<Queue> queue_;
  TestReportingPolicyWatcher reporting_policy_watcher_;

  size_t report_id_ = 1;

  NetworkWatcher network_watcher_;
  timekeeper::TestClock clock_;
  std::unique_ptr<stubs::NetworkReachabilityProvider> network_reachability_provider_;
  feedback::AnnotationManager annotation_manager_;
  std::unique_ptr<StubCrashServer> crash_server_;
  std::unique_ptr<ScopedTestReportStore> report_store_;
  std::shared_ptr<InfoContext> info_context_;
  std::shared_ptr<cobalt::Logger> cobalt_;
};

TEST_F(QueueTest, Add_ReportingPolicyUndecided) {
  SetUpQueue();

  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);
  const auto report_id = AddNewReport(/*is_hourly_report=*/false);

  ASSERT_TRUE(*report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));
}

TEST_F(QueueTest, Add_ReportingPolicyUndecided_HourlyReports) {
  SetUpQueue();

  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);
  const auto report_id_1 = AddNewReport(/*is_hourly_report=*/true);

  ASSERT_TRUE(*report_id_1);
  EXPECT_TRUE(queue_->Contains(*report_id_1));

  EXPECT_TRUE(queue_->HasHourlyReport());
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

TEST_F(QueueTest, Add_ReportingPolicyUpload) {
  SetUpQueue({kUploadSuccessful});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  auto report_id = AddNewReport(/*is_hourly_report=*/false);

  ASSERT_TRUE(*report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  report_id = AddNewReport(/*is_hourly_report=*/false);

  ASSERT_TRUE(*report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  report_id = AddNewReport(/*is_hourly_report=*/true);

  ASSERT_TRUE(*report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  EXPECT_EQ(queue_->Size(), 3u);
}

TEST_F(QueueTest, Upload) {
  SetUpQueue({kUploadSuccessful, kUploadFailed, kUploadSuccessful, kUploadFailed});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  std::vector<ReportId> report_ids;
  for (size_t i = 0; i < 4; ++i) {
    const auto report_id = AddNewReport(/*is_hourly_report=*/false);
    ASSERT_TRUE(report_id);
    ASSERT_TRUE(queue_->Contains(*report_id));
    report_ids.push_back(*report_id);
  }

  RunLoopFor(kUploadResponseDelay * report_ids.size());

  EXPECT_FALSE(queue_->Contains(report_ids[0]));
  EXPECT_TRUE(queue_->Contains(report_ids[1]));
  EXPECT_FALSE(queue_->Contains(report_ids[2]));
  EXPECT_TRUE(queue_->Contains(report_ids[3]));

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
              }));
}

TEST_F(QueueTest, SkipEmptyAnnotationUpload) {
  SetUpQueue({});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  std::vector<ReportId> report_ids;
  for (size_t i = 0; i < 4; ++i) {
    const auto report_id = AddNewReport(/*is_hourly_report=*/false, /*empty_annotations=*/true);
    ASSERT_TRUE(report_id);
    ASSERT_FALSE(queue_->Contains(*report_id));
    report_ids.push_back(*report_id);
  }

  RunLoopFor(kUploadResponseDelay * report_ids.size());

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::CrashState::kGarbageCollected),
                                          cobalt::Event(cobalt::CrashState::kGarbageCollected),
                                          cobalt::Event(cobalt::CrashState::kGarbageCollected),
                                          cobalt::Event(cobalt::CrashState::kGarbageCollected),
                                      }));
}

TEST_F(QueueTest, StopUploading) {
  SetUpQueue({kUploadFailed});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  std::vector<ReportId> report_ids;
  for (size_t i = 0; i < 3; ++i) {
    const auto report_id = AddNewReport(/*is_hourly_report=*/false);
    ASSERT_TRUE(*report_id);
    EXPECT_TRUE(queue_->Contains(*report_id));
    report_ids.push_back(*report_id);
  }

  queue_->StopUploading();
  RunLoopFor(kUploadResponseDelay);

  EXPECT_FALSE(queue_->IsPeriodicUploadScheduled());
  for (const auto& report_id : report_ids) {
    EXPECT_FALSE(queue_->Contains(report_id));
  }
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
              }));

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  EXPECT_FALSE(queue_->IsPeriodicUploadScheduled());
  const auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(*report_id);
  EXPECT_FALSE(queue_->Contains(*report_id));
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
              }));
}

TEST_F(QueueTest, PeriodicUpload) {
  SetUpQueue({
      kUploadFailed,
      kUploadFailed,
      kUploadFailed,
      kUploadSuccessful,
      kUploadSuccessful,
      kUploadSuccessful,
  });
  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);

  std::vector<ReportId> report_ids;
  for (size_t i = 0; i < 3; ++i) {
    auto report_id = AddNewReport(/*is_hourly_upload=*/false);

    ASSERT_TRUE(report_id);
    EXPECT_TRUE(queue_->Contains(*report_id));
    report_ids.push_back(*report_id);
  }

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  ASSERT_TRUE(queue_->IsPeriodicUploadScheduled());
  RunLoopFor(kUploadResponseDelay * report_ids.size());

  RunLoopFor(kPeriodicUploadDuration);
  for (const auto& report_id : report_ids) {
    EXPECT_FALSE(queue_->Contains(report_id));
  }

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
  EXPECT_FALSE(queue_->IsPeriodicUploadScheduled());
}

TEST_F(QueueTest, PeriodicUpload_AfterStopUploading) {
  SetUpQueue();

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  ASSERT_TRUE(queue_->IsPeriodicUploadScheduled());

  queue_->StopUploading();
  EXPECT_FALSE(queue_->IsPeriodicUploadScheduled());
}

TEST_F(QueueTest, UploadOnNetworkReachable) {
  SetUpQueue({
      kUploadFailed,
      kUploadFailed,
      kUploadFailed,
      kUploadSuccessful,
      kUploadSuccessful,
      kUploadSuccessful,
  });
  reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);

  std::vector<ReportId> report_ids;
  for (size_t i = 0; i < 3; ++i) {
    auto report_id = AddNewReport(/*is_hourly_upload=*/false);

    ASSERT_TRUE(report_id);
    EXPECT_TRUE(queue_->Contains(*report_id));
    report_ids.push_back(*report_id);
  }

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  ASSERT_TRUE(queue_->IsPeriodicUploadScheduled());
  RunLoopFor(kUploadResponseDelay * report_ids.size());

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopFor(kUploadResponseDelay * report_ids.size());
  for (const auto& report_id : report_ids) {
    EXPECT_FALSE(queue_->Contains(report_id));
  }

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
  EXPECT_TRUE(queue_->Contains(*report_id));

  report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  RunLoopFor(kUploadResponseDelay * 2);
  EXPECT_TRUE(queue_->Contains(*report_id));

  RunLoopFor(kPeriodicUploadDuration);
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

TEST_F(QueueTest, kUploadTimedOut) {
  SetUpQueue({kUploadTimedOut, kUploadFailed, kUploadTimedOut});

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  RunLoopFor(kUploadResponseDelay * 2);
  EXPECT_TRUE(queue_->Contains(*report_id));

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

TEST_F(QueueTest, SnapshotKeptAllReportsAdded) {
  SetUpQueue({kUploadSuccessful, kUploadSuccessful});
  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  // report_id_ will get incremented when we call AddNewReport. Add all clients before any get added
  // to Queue.
  queue_->AddReportUsingSnapshot(kSnapshotUuidValue, report_id_ + 1);
  queue_->AddReportUsingSnapshot(kSnapshotUuidValue, report_id_ + 2);

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = kAttachmentKey;
  FX_CHECK(fsl::VmoFromString("", &snapshot.value));

  GetSnapshotStore()->AddSnapshot(kSnapshotUuidValue, std::move(snapshot));
  ASSERT_TRUE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));

  const auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  const auto report_id2 = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id2);
  EXPECT_TRUE(queue_->Contains(*report_id2));

  RunLoopFor(kUploadResponseDelay);

  // Queue shouldn't delete the snapshot if there's still an internal client.
  ASSERT_FALSE(queue_->Contains(*report_id));
  ASSERT_TRUE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));

  RunLoopFor(kUploadResponseDelay);

  EXPECT_FALSE(queue_->Contains(*report_id2));
  EXPECT_FALSE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));
}

TEST_F(QueueTest, SnapshotKeptNotAllReportsAdded) {
  SetUpQueue({kUploadSuccessful, kUploadSuccessful});
  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  // report_id_ will get incremented when we call AddNewReport. Add all clients before any get added
  // to Queue.
  queue_->AddReportUsingSnapshot(kSnapshotUuidValue, report_id_ + 1);
  queue_->AddReportUsingSnapshot(kSnapshotUuidValue, report_id_ + 2);

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = kAttachmentKey;
  FX_CHECK(fsl::VmoFromString("", &snapshot.value));

  GetSnapshotStore()->AddSnapshot(kSnapshotUuidValue, std::move(snapshot));
  ASSERT_TRUE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));

  const auto report_id = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  RunLoopFor(kUploadResponseDelay);

  // Queue shouldn't delete the snapshot if there's still a client in SnapshotCollector.
  ASSERT_FALSE(queue_->Contains(*report_id));
  ASSERT_TRUE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));

  const auto report_id2 = AddNewReport(/*is_hourly_report=*/false);
  ASSERT_TRUE(report_id2);
  EXPECT_TRUE(queue_->Contains(*report_id2));

  RunLoopFor(kUploadResponseDelay);

  EXPECT_FALSE(queue_->Contains(*report_id2));
  EXPECT_FALSE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));
}

TEST_F(QueueTest, Check_SpecialCaseClientsRemoved) {
  SetUpQueue({
      kUploadSuccessful,
  });
  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = kAttachmentKey;
  FX_CHECK(fsl::VmoFromString("", &snapshot.value));

  GetSnapshotStore()->AddSnapshot(kSnapshotUuidValue, std::move(snapshot));
  ASSERT_TRUE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));

  ++report_id_;
  queue_->AddReportUsingSnapshot(kSnapshotUuidValue, report_id_);
  Report report = MakeReport(report_id_, {});

  // Modify report to have special case uuid.
  report.SnapshotUuid() = kShutdownSnapshotUuid;

  queue_->Add(std::move(report));

  EXPECT_TRUE(queue_->Contains(report_id_));

  // Queue should delete snapshot despite the report ending up with a special case snapshot uuid.
  EXPECT_FALSE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));
}

TEST_F(QueueTest, PreventStrandedSnapshot) {
  report_store_ = std::make_unique<ScopedTestReportStore>(
      &annotation_manager_, info_context_,
      /*max_reports_tmp_size=*/crash_reports::kReportStoreMaxTmpSize,
      /*max_reports_cache_size=*/StorageSize::Bytes(0),
      /*max_snapshots_tmp_size=*/StorageSize::Megabytes(1),
      /*max_snapshots_cache_size=*/StorageSize::Megabytes(1));

  // Fail the first 2 uploads so the reports get moved to /tmp. Fail the last upload so we
  // can verify the snapshot was moved from /cache to /tmp.
  SetUpQueue({
      kUploadFailed,
      kUploadFailed,
      kUploadSuccessful,
      kUploadFailed,
  });
  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  const auto report_id = AddNewReport(/*is_hourly_report=*/
                                      false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  const auto report_id2 = AddNewReport(/*is_hourly_report=*/
                                       false);
  ASSERT_TRUE(report_id2);
  EXPECT_TRUE(queue_->Contains(*report_id2));

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = kAttachmentKey;
  FX_CHECK(fsl::VmoFromString("", &snapshot.value));
  GetSnapshotStore()->AddSnapshot(kSnapshotUuidValue, std::move(snapshot));

  ASSERT_TRUE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));
  EXPECT_TRUE(GetSnapshotStore()->MoveToPersistence(kSnapshotUuidValue));
  ASSERT_EQ(GetSnapshotStore()->SnapshotLocation(kSnapshotUuidValue), ItemLocation::kCache);

  // Initial upload attempts + 15 minute retry.
  RunLoopFor(2 * kUploadResponseDelay + zx::min(15));

  EXPECT_EQ(GetSnapshotStore()->SnapshotLocation(kSnapshotUuidValue), ItemLocation::kTmp);
}

TEST_F(QueueTest, PreventStrandedSnapshot_FailedMove) {
  report_store_ = std::make_unique<ScopedTestReportStore>(
      &annotation_manager_, info_context_,
      /*max_reports_tmp_size=*/crash_reports::kReportStoreMaxTmpSize,
      /*max_reports_cache_size=*/StorageSize::Bytes(0),
      /*max_snapshots_tmp_size=*/StorageSize::Bytes(0),
      /*max_snapshots_cache_size=*/StorageSize::Megabytes(1));

  // Fail the first 2 uploads so the reports get moved to /tmp. Fail the 4th upload so we
  // can verify the snapshot was deleted after failing to move to /tmp. Succeed on the last upload
  // so we can verify the debug.snapshot annotations added to the 2nd report.
  SetUpQueue({
      kUploadFailed,
      kUploadFailed,
      kUploadSuccessful,
      kUploadFailed,
      kUploadSuccessful,
  });
  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);

  const auto report_id = AddNewReport(/*is_hourly_report=*/
                                      false);
  ASSERT_TRUE(report_id);
  EXPECT_TRUE(queue_->Contains(*report_id));

  const auto report_id2 = AddNewReport(/*is_hourly_report=*/
                                       false);
  ASSERT_TRUE(report_id2);
  EXPECT_TRUE(queue_->Contains(*report_id2));

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = kAttachmentKey;
  FX_CHECK(fsl::VmoFromString("", &snapshot.value));
  GetSnapshotStore()->AddSnapshot(kSnapshotUuidValue, std::move(snapshot));

  ASSERT_TRUE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));
  EXPECT_TRUE(GetSnapshotStore()->MoveToPersistence(kSnapshotUuidValue));
  ASSERT_EQ(GetSnapshotStore()->SnapshotLocation(kSnapshotUuidValue), ItemLocation::kCache);

  // Initial upload attempts + 15 minute retry.
  RunLoopFor(2 * kUploadResponseDelay + zx::min(15));

  EXPECT_FALSE(GetSnapshotStore()->SnapshotExists(kSnapshotUuidValue));

  RunLoopFor(zx::min(15));

  FX_CHECK(crash_server_);

  EXPECT_THAT(crash_server_->latest_annotations().Raw(),
              UnorderedElementsAreArray({
                  Pair(kAnnotationKey, kAnnotationValue),
                  Pair(feedback::kDebugSnapshotErrorKey, "failed move to tmp"),
                  Pair(feedback::kDebugSnapshotPresentKey, "false"),
              }));
}

TEST_F(QueueTest, Check_SnapshotClientsReloaded) {
  report_store_ = std::make_unique<ScopedTestReportStore>(
      &annotation_manager_, info_context_,
      /*max_reports_tmp_size=*/crash_reports::kReportStoreMaxTmpSize,
      /*max_reports_cache_size=*/crash_reports::kReportStoreMaxCacheSize,
      /*max_snapshots_tmp_size=*/StorageSize::Bytes(0),
      /*max_snapshots_cache_size=*/StorageSize::Megabytes(1));

  Report report = MakeReport(report_id_, /*empty_annotations=*/false);

  ++report_id_;
  Report report2 = MakeReport(report_id_, /*empty_annotations=*/false);

  const ReportId report_id = report.Id();
  const ReportId report2_id = report2.Id();

  std::vector<ReportId> garbage_collected_reports;
  report_store_->GetReportStore().Add(std::move(report), &garbage_collected_reports);
  report_store_->GetReportStore().Add(std::move(report2), &garbage_collected_reports);

  ASSERT_TRUE(garbage_collected_reports.empty());

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = kAttachmentKey;
  FX_CHECK(fsl::VmoFromString("", &snapshot.value));

  GetSnapshotStore()->AddSnapshot(kSnapshotUuidValue, std::move(snapshot));
  ASSERT_EQ(GetSnapshotStore()->SnapshotLocation(kSnapshotUuidValue), ItemLocation::kMemory);

  EXPECT_TRUE(GetSnapshotStore()->MoveToPersistence(kSnapshotUuidValue));
  ASSERT_EQ(GetSnapshotStore()->SnapshotLocation(kSnapshotUuidValue), ItemLocation::kCache);

  // Verify report clients are reloaded by checking if the snapshot gets deleted prematurely.
  SetUpQueue({
      kUploadSuccessful,
      kUploadSuccessful,
  });

  ASSERT_TRUE(queue_->Contains(report_id));
  ASSERT_TRUE(queue_->Contains(report2_id));

  reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
  network_reachability_provider_->TriggerOnNetworkReachable(true);

  RunLoopFor(kUploadResponseDelay);
  EXPECT_FALSE(queue_->Contains(report_id));
  EXPECT_TRUE(queue_->Contains(report2_id));

  ASSERT_EQ(GetSnapshotStore()->SnapshotLocation(kSnapshotUuidValue), ItemLocation::kCache);

  RunLoopFor(kUploadResponseDelay);
  EXPECT_FALSE(queue_->Contains(report_id));
  EXPECT_FALSE(queue_->Contains(report2_id));
  EXPECT_FALSE(GetSnapshotStore()->SnapshotLocation(kSnapshotUuidValue).has_value());
}

TEST_F(QueueTest, Check_CleansUpStrandedSnapshotsInCache) {
  report_store_ = std::make_unique<ScopedTestReportStore>(
      &annotation_manager_, info_context_,
      /*max_reports_tmp_size=*/crash_reports::kReportStoreMaxTmpSize,
      /*max_reports_cache_size=*/crash_reports::kReportStoreMaxCacheSize,
      /*max_snapshots_tmp_size=*/StorageSize::Bytes(0),
      /*max_snapshots_cache_size=*/StorageSize::Megabytes(1));

  SetUpQueue();

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = kAttachmentKey;
  FX_CHECK(fsl::VmoFromString("", &snapshot.value));

  const SnapshotUuid kTestUuid = "test uuid";
  GetSnapshotStore()->AddSnapshot(kTestUuid, std::move(snapshot));
  GetSnapshotStore()->MoveToPersistence(kTestUuid);

  ASSERT_EQ(GetSnapshotStore()->SnapshotLocation(kTestUuid), ItemLocation::kCache);

  // Queue should clean up stranded snapshots on construction.
  SetUpQueue();

  EXPECT_FALSE(GetSnapshotStore()->SnapshotExists(kTestUuid));
}

TEST_F(QueueTest, Check_CleansUpStrandedSnapshotsInTmp) {
  report_store_ = std::make_unique<ScopedTestReportStore>(
      &annotation_manager_, info_context_,
      /*max_reports_tmp_size=*/crash_reports::kReportStoreMaxTmpSize,
      /*max_reports_cache_size=*/crash_reports::kReportStoreMaxCacheSize,
      /*max_snapshots_tmp_size=*/StorageSize::Megabytes(1),
      /*max_snapshots_cache_size=*/StorageSize::Bytes(0));

  SetUpQueue();

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = kAttachmentKey;
  FX_CHECK(fsl::VmoFromString("", &snapshot.value));

  const SnapshotUuid kTestUuid = "test uuid";
  GetSnapshotStore()->AddSnapshot(kTestUuid, std::move(snapshot));
  GetSnapshotStore()->MoveToPersistence(kTestUuid);

  ASSERT_EQ(GetSnapshotStore()->SnapshotLocation(kTestUuid), ItemLocation::kTmp);

  // Queue should clean up stranded snapshots on construction.
  SetUpQueue();

  EXPECT_FALSE(GetSnapshotStore()->SnapshotExists(kTestUuid));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
