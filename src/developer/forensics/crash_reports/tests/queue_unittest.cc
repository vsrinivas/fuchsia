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

constexpr char kStorePath[] = "/tmp/reports";

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
  files::ReadDirContents(kStorePath, &program_shortnames);
  RemoveCurDir(&program_shortnames);
  for (const auto& program_shortname : program_shortnames) {
    const std::string path = files::JoinPath(kStorePath, program_shortname);

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

std::map<std::string, std::string> MakeAnnotations() {
  return {{kAnnotationKey, kAnnotationValue}};
}

Report MakeReport(const std::size_t report_id) {
  std::optional<Report> report =
      Report::MakeReport(report_id, fxl::StringPrintf("program_%ld", report_id), MakeAnnotations(),
                         MakeAttachments(), kSnapshotUuidValue, BuildAttachment(kMinidumpValue));
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
  QueueTest() : network_watcher_(dispatcher(), services()) {}

  void SetUp() override {
    info_context_ =
        std::make_shared<InfoContext>(&InspectRoot(), &clock_, dispatcher(), services());

    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    SetUpNetworkReachabilityProvider();
    RunLoopUntilIdle();
  }

  void TearDown() override { ASSERT_TRUE(files::DeletePath(kStorePath, /*recursive=*/true)); }

 protected:
  void SetUpNetworkReachabilityProvider() {
    network_reachability_provider_ = std::make_unique<stubs::NetworkReachabilityProvider>();
    InjectServiceProvider(network_reachability_provider_.get());
  }

  void SetUpQueue(std::vector<CrashServer::UploadStatus> upload_attempt_results =
                      std::vector<CrashServer::UploadStatus>{}) {
    report_id_ = 1;
    reporting_policy_ = QueueOps::SetReportingPolicyUndecided;
    expected_queue_contents_.clear();
    upload_attempt_results_ = upload_attempt_results;
    next_upload_attempt_result_ = upload_attempt_results_.cbegin();
    snapshot_manager_ =
        std::make_unique<SnapshotManager>(dispatcher(), services(), &clock_, zx::sec(5),
                                          StorageSize::Gigabytes(1), StorageSize::Gigabytes(1));
    crash_server_ = std::make_unique<StubCrashServer>(upload_attempt_results_);
    crash_server_->AddSnapshotManager(snapshot_manager_.get());

    queue_ = std::make_unique<Queue>(dispatcher(), services(), info_context_, &tags_,
                                     crash_server_.get(), snapshot_manager_.get());
    ASSERT_TRUE(queue_);
    queue_->WatchReportingPolicy(&reporting_policy_watcher_);
    queue_->WatchNetwork(&network_watcher_);
  }

  enum class QueueOps {
    AddNewReport,
    DeleteOneReport,
    SetReportingPolicyArchive,
    SetReportingPolicyDelete,
    SetReportingPolicyUpload,
    SetReportingPolicyUndecided,
  };

  void ApplyQueueOps(const std::vector<QueueOps>& ops) {
    for (auto const& op : ops) {
      switch (op) {
        case QueueOps::AddNewReport:
          FX_CHECK(queue_->Add(MakeReport(report_id_)));
          AddExpectedReport(report_id_);
          RunLoopUntilIdle();
          ++report_id_;
          break;
        case QueueOps::DeleteOneReport:
          if (!expected_queue_contents_.empty()) {
            std::optional<std::string> report_id = DeleteReportFromStore();
            if (report_id.has_value()) {
              expected_queue_contents_.erase(
                  std::remove(expected_queue_contents_.begin(), expected_queue_contents_.end(),
                              std::stoull(report_id.value())),
                  expected_queue_contents_.end());
            }
          }
          break;
        case QueueOps::SetReportingPolicyDelete:
          reporting_policy_ = QueueOps::SetReportingPolicyDelete;
          reporting_policy_watcher_.Set(ReportingPolicy::kDoNotFileAndDelete);
          expected_queue_contents_.clear();
          break;
        case QueueOps::SetReportingPolicyArchive:
          reporting_policy_ = QueueOps::SetReportingPolicyArchive;
          reporting_policy_watcher_.Set(ReportingPolicy::kArchive);
          break;
        case QueueOps::SetReportingPolicyUpload:
          reporting_policy_ = QueueOps::SetReportingPolicyUpload;
          reporting_policy_watcher_.Set(ReportingPolicy::kUpload);
          SimulateUploadAll();
          break;
        case QueueOps::SetReportingPolicyUndecided:
          reporting_policy_ = QueueOps::SetReportingPolicyUndecided;
          reporting_policy_watcher_.Set(ReportingPolicy::kUndecided);
          break;
      }
    }
    RunLoopUntilIdle();
  }

  void SimulatePerioidUpload() {
    RunLoopFor(kPeriodicUploadDuration);
    SimulateUploadAll();
  }

  void SimulateNetworkReachable() {
    network_reachability_provider_->TriggerOnNetworkReachable(true);
    RunLoopUntilIdle();
    SimulateUploadAll();
  }

  void CheckQueueContents() {
    for (const auto& id : expected_queue_contents_) {
      EXPECT_TRUE(queue_->Contains(id));
    }
    EXPECT_EQ(queue_->Size(), expected_queue_contents_.size());
  }

  void CheckAnnotationsOnServer() {
    FX_CHECK(crash_server_);

    // Expect annotations that |snapshot_manager_| will for using |kSnapshotUuidValue| as the
    // snapshot uuid.
    EXPECT_THAT(crash_server_->latest_annotations(),
                UnorderedElementsAreArray({
                    Pair(kAnnotationKey, kAnnotationValue),
                    Pair("debug.snapshot.error", "garbage collected"),
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
  std::vector<ReportId> expected_queue_contents_;

 private:
  void SimulateUploadAll() {
    if (reporting_policy_ != QueueOps::SetReportingPolicyUpload) {
      return;
    }

    std::vector<ReportId> new_expected_queue_contents;
    for (const auto& id : expected_queue_contents_) {
      if (*next_upload_attempt_result_ == CrashServer::UploadStatus::kFailure) {
        new_expected_queue_contents.push_back(id);
      }

      ++next_upload_attempt_result_;
    }
    expected_queue_contents_.swap(new_expected_queue_contents);
  }

  void AddExpectedReport(const ReportId& uuid) {
    // Add a report to the back of the expected queue contents if and only if it is expected
    // to be in the queue after processing.
    switch (reporting_policy_) {
      case QueueOps::SetReportingPolicyUpload:
        if (*next_upload_attempt_result_ == CrashServer::UploadStatus::kFailure) {
          expected_queue_contents_.push_back(uuid);
        }
        ++next_upload_attempt_result_;
        break;
      case QueueOps::SetReportingPolicyUndecided:
        expected_queue_contents_.push_back(uuid);
        break;
      case QueueOps::SetReportingPolicyDelete:
      case QueueOps::SetReportingPolicyArchive:
        break;
      case QueueOps::AddNewReport:
      case QueueOps::DeleteOneReport:
        FX_CHECK(false);
        break;
    }
  }

  size_t report_id_ = 1;
  QueueOps reporting_policy_ = QueueOps::SetReportingPolicyUndecided;
  std::vector<CrashServer::UploadStatus> upload_attempt_results_;
  std::vector<CrashServer::UploadStatus>::const_iterator next_upload_attempt_result_;

  TestReportingPolicyWatcher reporting_policy_watcher_;
  NetworkWatcher network_watcher_;
  timekeeper::TestClock clock_;
  std::unique_ptr<stubs::NetworkReachabilityProvider> network_reachability_provider_;
  std::unique_ptr<SnapshotManager> snapshot_manager_;
  std::unique_ptr<StubCrashServer> crash_server_;
  std::shared_ptr<InfoContext> info_context_;
  std::shared_ptr<cobalt::Logger> cobalt_;
};

TEST_F(QueueTest, Check_Add_ReportingPolicyUndecided) {
  SetUpQueue();
  ApplyQueueOps({
      QueueOps::SetReportingPolicyUndecided,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  EXPECT_EQ(queue_->Size(), 1u);
}

TEST_F(QueueTest, Check_Add_ReportingPolicyDoNotFileAndDelete) {
  SetUpQueue({kUploadFailed});
  ApplyQueueOps({
      QueueOps::SetReportingPolicyUpload,
      QueueOps::AddNewReport,
      QueueOps::SetReportingPolicyDelete,
  });
  CheckQueueContents();
  EXPECT_TRUE(queue_->IsEmpty());

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kDeleted),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kDeleted, 1u),
              }));
}

TEST_F(QueueTest, Check_Add_ReportingPolicyArchive) {
  SetUpQueue();
  ApplyQueueOps({
      QueueOps::SetReportingPolicyArchive,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  EXPECT_TRUE(queue_->IsEmpty());

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::CrashState::kArchived),
                                      }));
}

TEST_F(QueueTest, Check_Add_ReportingPolicyUpload_SuccessfulEarlyUpload) {
  SetUpQueue({kUploadSuccessful});
  ApplyQueueOps({
      QueueOps::SetReportingPolicyUpload,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
              }));
}

TEST_F(QueueTest, Check_Add_ReportingPolicyUpload_FailedEarlyUpload) {
  SetUpQueue({kUploadFailed});
  ApplyQueueOps({
      QueueOps::SetReportingPolicyUpload,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  EXPECT_EQ(queue_->Size(), 1u);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
              }));
}

TEST_F(QueueTest, Check_UploadTaskRunsPeriodically) {
  SetUpQueue({kUploadSuccessful, kUploadSuccessful});
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::SetReportingPolicyUpload,
  });
  SimulatePerioidUpload();

  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());

  ApplyQueueOps({
      QueueOps::AddNewReport,
  });
  SimulatePerioidUpload();

  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
              }));
}

TEST_F(QueueTest, Check_ReportingPolicyChangesCancelUploadTask) {
  SetUpQueue({kUploadFailed, kUploadFailed});
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::SetReportingPolicyUpload,
      QueueOps::SetReportingPolicyUndecided,
  });
  SimulatePerioidUpload();

  CheckQueueContents();
  EXPECT_EQ(queue_->Size(), 1u);

  ApplyQueueOps({
      QueueOps::SetReportingPolicyUpload,
      QueueOps::SetReportingPolicyDelete,
  });
  SimulatePerioidUpload();

  CheckQueueContents();
  EXPECT_TRUE(queue_->IsEmpty());

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kDeleted),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kDeleted, 2u),
              }));
}

TEST_F(QueueTest, Check_UploadOnNetworkReachable) {
  SetUpQueue({kUploadFailed, kUploadSuccessful});

  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::SetReportingPolicyUpload,
  });
  EXPECT_EQ(queue_->Size(), 1u);

  SimulateNetworkReachable();

  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 2u),
              }));
}

TEST_F(QueueTest, Check_ReportGarbageCollected) {
  SetUpQueue({kUploadSuccessful});
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::DeleteOneReport,
      QueueOps::SetReportingPolicyUpload,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  EXPECT_TRUE(queue_->IsEmpty());

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
              }));
}

TEST_F(QueueTest, Check_EarlyUploadThrottled) {
  SetUpQueue({kUploadThrottled});
  ApplyQueueOps({
      QueueOps::SetReportingPolicyUpload,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploadThrottled),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadThrottled, 1u),
              }));
}

TEST_F(QueueTest, Check_ThrottledReportDropped) {
  SetUpQueue({kUploadThrottled});
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::SetReportingPolicyUpload,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploadThrottled),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadThrottled, 1u),
              }));
}

TEST_F(QueueTest, Check_CobaltMultipleUploadAttempts) {
  SetUpQueue({
      kUploadFailed,
      kUploadSuccessful,
      kUploadSuccessful,
  });
  ApplyQueueOps({
      QueueOps::SetReportingPolicyUpload,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
  });

  RunLoopFor(kPeriodicUploadDuration);
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  // Two reports were eventually uploaded.
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  // The first report required two tries.
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 2u),
                  // The second report only needed one try.
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
              }));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
