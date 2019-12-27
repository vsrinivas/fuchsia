// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/queue.h"

#include <lib/inspect/cpp/inspect.h>
#include <lib/timekeeper/test_clock.h>

#include <memory>

#include "sdk/lib/inspect/testing/cpp/inspect.h"
#include "src/developer/feedback/crashpad_agent/constants.h"
#include "src/developer/feedback/crashpad_agent/info/info_context.h"
#include "src/developer/feedback/crashpad_agent/settings.h"
#include "src/developer/feedback/crashpad_agent/tests/stub_crash_server.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using crashpad::CrashReportDatabase;
using crashpad::UUID;
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

constexpr char kCrashpadDatabasePath[] = "/tmp/crashes";

constexpr char kAttachmentKey[] = "attachment.key";
constexpr char kAttachmentValue[] = "attachment.value";
constexpr char kAnnotationKey[] = "annotation.key";
constexpr char kAnnotationValue[] = "annotation.value";
constexpr char kMinidumpKey[] = "uploadFileMinidump";
constexpr char kMinidumpValue[] = "minidump";

fuchsia::mem::Buffer BuildAttachment(const std::string& value) {
  fuchsia::mem::Buffer attachment;
  FXL_CHECK(fsl::VmoFromString(value, &attachment));
  return attachment;
}

std::map<std::string, fuchsia::mem::Buffer> MakeAttachments() {
  std::map<std::string, fuchsia::mem::Buffer> attachments;
  attachments[kAttachmentKey] = BuildAttachment(kAttachmentValue);
  return attachments;
}

std::map<std::string, std::string> MakeAnnotations() {
  return {{kAnnotationKey, kAnnotationValue}};
}

class QueueTest : public UnitTestFixture {
 public:
  void TearDown() override {
    ASSERT_TRUE(files::DeletePath(kCrashpadDatabasePath, /*recursive=*/true));
  }

  void SetUpQueue(std::vector<bool> upload_attempt_results = std::vector<bool>{}) {
    program_id_ = 1;
    state_ = QueueOps::SetStateToLeaveAsPending;
    upload_attempt_results_ = upload_attempt_results;
    next_upload_attempt_result_ = upload_attempt_results_.cbegin();
    expected_queue_contents_.clear();

    settings_.set_upload_policy(UploadPolicy::LIMBO);
    clock_ = std::make_unique<timekeeper::TestClock>();
    crash_server_ = std::make_unique<StubCrashServer>(upload_attempt_results_);
    inspector_ = std::make_unique<inspect::Inspector>();
    info_context_ = std::make_shared<InfoContext>(&inspector_->GetRoot(), clock_.get(),
                                                  dispatcher(), services());
    queue_ = Queue::TryCreate(dispatcher(), info_context_, crash_server_.get());

    ASSERT_TRUE(queue_);

    queue_->WatchSettings(&settings_);
  }

 protected:
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

          ASSERT_TRUE(queue_->Add(fxl::StringPrintf("program_%ld", program_id_),
                                  /*attachments=*/MakeAttachments(),
                                  /*minidump=*/BuildAttachment(kMinidumpValue),
                                  /*annotations=*/MakeAnnotations()));
          ASSERT_TRUE(RunLoopUntilIdle());
          ++program_id_;
          if (!queue_->IsEmpty()) {
            AddExpectedReport(queue_->LatestReport());
          }
          SetExpectedQueueContents();
          break;
        case QueueOps::DeleteOneReport:
          if (!expected_queue_contents_.empty()) {
            // Create a database in order to delete a report.
            auto database_ = CrashReportDatabase::InitializeWithoutCreating(
                base::FilePath(kCrashpadDatabasePath));
            ASSERT_TRUE(database_);
            database_->DeleteReport(expected_queue_contents_.back());
            expected_queue_contents_.pop_back();
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
    FXL_CHECK(crash_server_);
    EXPECT_THAT(crash_server_->latest_annotations(),
                UnorderedElementsAre(testing::Pair(kAnnotationKey, kAnnotationValue)));
  }

  void CheckAttachmentKeysOnServer() {
    FXL_CHECK(crash_server_);
    EXPECT_THAT(crash_server_->latest_attachment_keys(),
                UnorderedElementsAre(kAttachmentKey, kMinidumpKey));
  }

  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FXL_CHECK(result.is_ok());
    return result.take_value();
  }

  std::unique_ptr<Queue> queue_;
  std::vector<UUID> expected_queue_contents_;

 private:
  void AddExpectedReport(const UUID& uuid) {
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
      std::vector<UUID> new_queue_contents;
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
  std::unique_ptr<timekeeper::TestClock> clock_;
  std::unique_ptr<StubCrashServer> crash_server_;
  std::unique_ptr<inspect::Inspector> inspector_;
  std::shared_ptr<InfoContext> info_context_;
  std::shared_ptr<Cobalt> cobalt_;
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

TEST_F(QueueTest, Check_IsEmptyQueue_OnStateSetToArchive_MultipleReports_OnePruned) {
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

TEST_F(QueueTest, Check_IsEmptyQueue_OnSuccessfulUpload) {
  SetUpQueue({kUploadSuccessful});
  ApplyQueueOps({
      QueueOps::SetStateToUpload,
      QueueOps::AddNewReport,
  });
  CheckQueueContents();
  CheckAnnotationsOnServer();
  CheckAttachmentKeysOnServer();
  EXPECT_TRUE(queue_->IsEmpty());
}

TEST_F(QueueTest, Check_NotIsEmptyQueue_OnFailedUpload) {
  SetUpQueue({kUploadFailed});
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

TEST_F(QueueTest, Check_IsEmptyQueue_OnSuccessfulUpload_OnePruned) {
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

TEST_F(QueueTest, Check_IsEmptyQueue_OnSuccessfulUpload_MultiplePruned_MultipleReports) {
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

TEST_F(QueueTest, Check_NotIsEmptyQueue_OnMixedUploadResults_MultiplePruned_MultipleReports) {
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

TEST_F(QueueTest, Check_InspectTree) {
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
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("reports")),
          ChildrenMatch(IsSupersetOf({
              AllOf(NodeMatches(NameMatches("program_1")),
                    ChildrenMatch(
                        ElementsAre(AllOf(NodeMatches(AllOf(PropertyList(UnorderedElementsAreArray({
                                              StringIs("creation_time", Not(IsEmpty())),
                                              StringIs("final_state", "uploaded"),
                                              UintIs("upload_attempts", 1u),
                                          })))),
                                          ChildrenMatch(ElementsAre(NodeMatches(
                                              AllOf(NameMatches("crash_server"),
                                                    PropertyList(UnorderedElementsAreArray({
                                                        StringIs("creation_time", Not(IsEmpty())),
                                                        StringIs("id", kStubServerReportId),
                                                    })))))))))),
              AllOf(NodeMatches(NameMatches("program_2")),
                    ChildrenMatch(
                        ElementsAre(AllOf(NodeMatches(AllOf(PropertyList(UnorderedElementsAreArray({
                                              StringIs("creation_time", Not(IsEmpty())),
                                              StringIs("final_state", "uploaded"),
                                              UintIs("upload_attempts", 1u),
                                          })))),
                                          ChildrenMatch(ElementsAre(NodeMatches(
                                              AllOf(NameMatches("crash_server"),
                                                    PropertyList(UnorderedElementsAreArray({
                                                        StringIs("creation_time", Not(IsEmpty())),
                                                        StringIs("id", kStubServerReportId),
                                                    })))))))))),
              AllOf(NodeMatches(NameMatches("program_3")),
                    ChildrenMatch(UnorderedElementsAreArray({
                        NodeMatches(AllOf(NameMatches(expected_queue_contents_[0].ToString()),
                                          PropertyList(UnorderedElementsAreArray({
                                              StringIs("creation_time", Not(IsEmpty())),
                                              UintIs("upload_attempts", 1u),
                                          })))),
                    }))),
              AllOf(NodeMatches(NameMatches("program_4")),
                    ChildrenMatch(UnorderedElementsAreArray({
                        NodeMatches(AllOf(NameMatches(expected_queue_contents_[1].ToString()),
                                          PropertyList(UnorderedElementsAreArray({
                                              StringIs("creation_time", Not(IsEmpty())),
                                              UintIs("upload_attempts", 1u),
                                          })))),
                    }))),
              AllOf(NodeMatches(NameMatches("program_5")),
                    ChildrenMatch(
                        ElementsAre(AllOf(NodeMatches(AllOf(PropertyList(UnorderedElementsAreArray({
                                              StringIs("creation_time", Not(IsEmpty())),
                                              StringIs("final_state", "uploaded"),
                                              UintIs("upload_attempts", 1u),
                                          })))),
                                          ChildrenMatch(ElementsAre(NodeMatches(
                                              AllOf(NameMatches("crash_server"),
                                                    PropertyList(UnorderedElementsAreArray({
                                                        StringIs("creation_time", Not(IsEmpty())),
                                                        StringIs("id", kStubServerReportId),
                                                    })))))))))),
          }))))));
}

}  // namespace
}  // namespace feedback
