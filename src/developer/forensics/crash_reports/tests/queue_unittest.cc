// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/queue.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/settings.h"
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

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using testing::Contains;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::IsSupersetOf;
using testing::Not;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

using UploadPolicy = Settings::UploadPolicy;

constexpr bool kUploadSuccessful = true;
constexpr bool kUploadFailed = false;

constexpr char kStorePath[] = "/tmp/reports";

constexpr char kAttachmentKey[] = "attachment.key";
constexpr char kAttachmentValue[] = "attachment.value";
constexpr char kAnnotationKey[] = "annotation.key";
constexpr char kAnnotationValue[] = "annotation.value";
constexpr char kMinidumpKey[] = "uploadFileMinidump";
constexpr char kMinidumpValue[] = "minidump";

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

Report MakeReport(const std::size_t program_id) {
  std::optional<Report> report =
      Report::MakeReport(fxl::StringPrintf("program_%ld", program_id), MakeAnnotations(),
                         MakeAttachments(), BuildAttachment(kMinidumpValue));
  FX_CHECK(report.has_value());
  return std::move(report.value());
}

class QueueTest : public UnitTestFixture {
 public:
  void SetUp() override {
    settings_.set_upload_policy(UploadPolicy::LIMBO);
    info_context_ = std::make_shared<InfoContext>(&InspectRoot(), clock_, dispatcher(), services());

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

  void SetUpQueue(std::vector<bool> upload_attempt_results = std::vector<bool>{}) {
    program_id_ = 1;
    state_ = QueueOps::SetStateToLeaveAsPending;
    expected_queue_contents_.clear();
    upload_attempt_results_ = upload_attempt_results;
    next_upload_attempt_result_ = upload_attempt_results_.cbegin();
    crash_server_ = std::make_unique<StubCrashServer>(upload_attempt_results_);

    queue_ = std::make_unique<Queue>(dispatcher(), services(), info_context_, crash_server_.get());
    ASSERT_TRUE(queue_);
    queue_->WatchSettings(&settings_);
  }

  enum class QueueOps {
    AddNewReport,
    DeleteOneReport,
    SetStateToArchive,
    SetStateToUpload,
    SetStateToLeaveAsPending,
    ProcessAll,
  };

  void ApplyQueueOps(const std::vector<QueueOps>& ops) {
    for (auto const& op : ops) {
      switch (op) {
        case QueueOps::AddNewReport:
          FX_CHECK(queue_->Add(MakeReport(program_id_)));
          FX_CHECK(RunLoopUntilIdle());
          ++program_id_;
          if (!queue_->IsEmpty()) {
            AddExpectedReport(queue_->LatestReport());
          }
          SetExpectedQueueContents();
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
          SetExpectedQueueContents();
          break;
        case QueueOps::SetStateToArchive:
          state_ = QueueOps::SetStateToArchive;
          settings_.set_upload_policy(UploadPolicy::DISABLED);
          SetExpectedQueueContents();
          break;
        case QueueOps::SetStateToUpload:
          state_ = QueueOps::SetStateToUpload;
          settings_.set_upload_policy(UploadPolicy::ENABLED);
          SetExpectedQueueContents();
          break;
        case QueueOps::SetStateToLeaveAsPending:
          state_ = QueueOps::SetStateToLeaveAsPending;
          settings_.set_upload_policy(UploadPolicy::LIMBO);
          SetExpectedQueueContents();
          break;
        case QueueOps::ProcessAll:
          EXPECT_EQ(SetExpectedQueueContents(), queue_->ProcessAll());
          break;
      }
    }
  }

  void CheckQueueContents() {
    for (const auto& id : expected_queue_contents_) {
      EXPECT_TRUE(queue_->Contains(id));
    }
    EXPECT_EQ(queue_->Size(), expected_queue_contents_.size());
  }

  void CheckAnnotationsOnServer() {
    FX_CHECK(crash_server_);
    EXPECT_THAT(crash_server_->latest_annotations(),
                UnorderedElementsAre(testing::Pair(kAnnotationKey, kAnnotationValue)));
  }

  void CheckAttachmentKeysOnServer() {
    FX_CHECK(crash_server_);
    EXPECT_THAT(crash_server_->latest_attachment_keys(),
                UnorderedElementsAre(kAttachmentKey, kMinidumpKey));
  }

  std::unique_ptr<Queue> queue_;
  std::vector<Store::Uid> expected_queue_contents_;
  std::unique_ptr<stubs::NetworkReachabilityProvider> network_reachability_provider_;

 private:
  void AddExpectedReport(const Store::Uid& uuid) {
    // Add a report to the back of the expected queue contents if and only if it is expected
    // to be in the queue after processing.
    if (state_ != QueueOps::SetStateToUpload) {
      expected_queue_contents_.push_back(uuid);
    } else if (!*next_upload_attempt_result_) {
      expected_queue_contents_.push_back(uuid);
      ++next_upload_attempt_result_;
    }
  }

  size_t SetExpectedQueueContents() {
    if (state_ == QueueOps::SetStateToArchive) {
      const size_t old_size = expected_queue_contents_.size();
      expected_queue_contents_.clear();
      return old_size;
    } else if (state_ == QueueOps::SetStateToUpload) {
      std::vector<Store::Uid> new_queue_contents;
      for (const auto& uuid : expected_queue_contents_) {
        // We expect the reports we failed to upload to still be pending.
        if (!(*next_upload_attempt_result_)) {
          new_queue_contents.push_back(uuid);
        }
        ++next_upload_attempt_result_;
      }
      expected_queue_contents_.swap(new_queue_contents);
      return new_queue_contents.size() - expected_queue_contents_.size();
    }
    return 0;
  }

  size_t program_id_ = 1;
  QueueOps state_ = QueueOps::SetStateToLeaveAsPending;
  std::vector<bool> upload_attempt_results_;
  std::vector<bool>::const_iterator next_upload_attempt_result_;

  Settings settings_;
  timekeeper::TestClock clock_;
  std::unique_ptr<StubCrashServer> crash_server_;
  std::shared_ptr<InfoContext> info_context_;
  std::shared_ptr<cobalt::Logger> cobalt_;
};

TEST_F(QueueTest, Check_EmptyQueue_OnZeroAdds) {
  SetUpQueue();
  CheckQueueContents();
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_NotIsEmptyQueue_OnStateSetToLeaveAsPending_MultipleReports) {
  SetUpQueue();
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::ProcessAll,
  });
  CheckQueueContents();
  EXPECT_EQ(queue_->Size(), 5u);
}

TEST_F(QueueTest, Check_IsEmptyQueue_OnStateSetToArchive_MultipleReports) {
  SetUpQueue();
  ApplyQueueOps({
      QueueOps::SetStateToArchive,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_IsEmptyQueue_OnStateSetToArchive_MultipleReports_OneGarbageCollected) {
  SetUpQueue();
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::DeleteOneReport,
      QueueOps::SetStateToArchive,
      QueueOps::ProcessAll,
  });
  CheckQueueContents();
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_EarlyUploadSucceeds) {
  SetUpQueue({kUploadSuccessful});
  ApplyQueueOps({
      QueueOps::SetStateToUpload,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
              }));
}

TEST_F(QueueTest, Check_EarlyUploadFails_ReattemptSucceeds) {
  SetUpQueue({kUploadFailed, kUploadSuccessful});
  ApplyQueueOps({
      QueueOps::SetStateToUpload,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 2u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 2u),
              }));
}

TEST_F(QueueTest, Check_EarlyUploadFails_ReattemptFails) {
  SetUpQueue({kUploadFailed, kUploadFailed});
  ApplyQueueOps({
      QueueOps::SetStateToUpload,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  EXPECT_EQ(queue_->Size(), 1u);
}

TEST_F(QueueTest, Check_IsEmptyQueue_OnSuccessfulUpload_MultipleReports) {
  SetUpQueue(std::vector<bool>(5u, kUploadSuccessful));
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
      QueueOps::ProcessAll,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_NotIsEmptyQueue_OnFailedUpload_MultipleReports) {
  SetUpQueue(std::vector<bool>(5u, kUploadFailed));
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_FALSE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_IsEmptyQueue_OnSuccessfulUpload_OneGarbageCollected) {
  SetUpQueue({kUploadSuccessful});
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::DeleteOneReport,
      QueueOps::SetStateToUpload,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_IsEmptyQueue_OnSuccessfulUpload_MultipleGarbageCollected_MultipleReports) {
  SetUpQueue({kUploadSuccessful});
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::DeleteOneReport,
      QueueOps::AddNewReport,
      QueueOps::DeleteOneReport,
      QueueOps::AddNewReport,
      QueueOps::DeleteOneReport,
      QueueOps::AddNewReport,
      QueueOps::DeleteOneReport,
      QueueOps::AddNewReport,
      QueueOps::DeleteOneReport,
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_NotIsEmptyQueue_OnMixedUploadResults_MultipleReports) {
  SetUpQueue({
      kUploadSuccessful,
      kUploadSuccessful,
      kUploadFailed,
      kUploadFailed,
      kUploadSuccessful,
  });
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_EQ(queue_->Size(), 2u);
}

TEST_F(QueueTest,
       Check_NotIsEmptyQueue_OnMixedUploadResults_MultipleGarbageCollected_MultipleReports) {
  SetUpQueue({
      kUploadSuccessful,
      kUploadSuccessful,
      kUploadFailed,
      kUploadFailed,
      kUploadSuccessful,
  });
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::DeleteOneReport,
      QueueOps::DeleteOneReport,
      QueueOps::SetStateToUpload,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_EQ(queue_->Size(), 2u);
}

TEST_F(QueueTest, Check_ProcessAll_ScheduledTwice) {
  SetUpQueue({
      kUploadFailed,
      kUploadSuccessful,
      kUploadFailed,
      kUploadSuccessful,
  });
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
  });
  ASSERT_FALSE(queue_->IsEmpty());

  RunLoopFor(zx::hour(1));
  EXPECT_TRUE(queue_->IsEmpty());

  ApplyQueueOps({
      QueueOps::SetStateToLeaveAsPending,
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
  });
  ASSERT_FALSE(queue_->IsEmpty());

  RunLoopFor(zx::hour(1));
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_ProcessAllTwice_OnNetworkReachable) {
  // Setup crash report upload outcome
  SetUpQueue({
      // First crash report: automatic upload fails (no early upload as upload not enabled at
      // first), succeed when the network becomes reachable.
      kUploadFailed,
      kUploadSuccessful,
      // Second crash report: automatic upload fails (no early upload as upload not enabled at
      // first), succeed when then network becomes reachable.
      kUploadFailed,
      kUploadSuccessful,
  });

  // First crash report: automatic upload fails. Succeed on the second upload attempt when the
  // network becomes reachable.
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
  });
  ASSERT_FALSE(queue_->IsEmpty());
  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_TRUE(queue_->IsEmpty());

  // Second crash report: Insert a new crash report that fails to upload at first,
  // and then check that it gets uploaded when the network becomes reachable again.
  ApplyQueueOps({
      QueueOps::SetStateToLeaveAsPending,
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
  });
  ASSERT_FALSE(queue_->IsEmpty());
  network_reachability_provider_->TriggerOnNetworkReachable(false);
  RunLoopUntilIdle();
  EXPECT_FALSE(queue_->IsEmpty());
  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_ProcessAll_OnReconnect_NetworkReachable) {
  // Setup crash report upload outcome: Automatic upload fails,
  // succeed when the network becomes reachable
  SetUpQueue({
      kUploadFailed,
      kUploadSuccessful,
  });

  // Automatic crash report upload fails.
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
  });
  ASSERT_FALSE(queue_->IsEmpty());

  // Close the connection to the network reachability service.
  network_reachability_provider_->CloseConnection();

  // We run the loop longer than the delay to account for the nondeterminism of
  // backoff::ExponentialBackoff.
  RunLoopFor(zx::min(3));

  // We should be re-connected to the network reachability service.
  // Test upload on network reachable.
  network_reachability_provider_->TriggerOnNetworkReachable(false);
  RunLoopUntilIdle();
  EXPECT_FALSE(queue_->IsEmpty());
  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_Cobalt) {
  SetUpQueue({
      kUploadSuccessful,
      kUploadSuccessful,
      kUploadFailed,
      kUploadFailed,
      kUploadSuccessful,
  });
  ApplyQueueOps({
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::AddNewReport,
      QueueOps::SetStateToUpload,
      QueueOps::SetStateToArchive,
  });

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::CrashState::kUploaded),
                  cobalt::Event(cobalt::CrashState::kArchived),
                  cobalt::Event(cobalt::CrashState::kArchived),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploaded, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kArchived, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kUploadAttempt, 1u),
                  cobalt::Event(cobalt::UploadAttemptState::kArchived, 1u),
              }));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
