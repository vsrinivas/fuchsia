// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/info/inspect_manager.h"

#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/timekeeper/test_clock.h>
#include <lib/zx/time.h>

#include <cstdint>
#include <memory>

#include "sdk/lib/inspect/testing/cpp/inspect.h"
#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/constants.h"
#include "src/developer/feedback/crashpad_agent/settings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
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
using testing::Not;
using testing::UnorderedElementsAreArray;

constexpr zx::time_utc kTime1(0);
constexpr zx::time_utc kTime2((zx::hour(7) + zx::min(14) + zx::sec(52)).get());
constexpr zx::time_utc kTime3((zx::hour(3) * 24 + zx::hour(15) + zx::min(33) + zx::sec(17)).get());

constexpr char kTime1Str[] = "1970-01-01 00:00:00 GMT";
constexpr char kTime2Str[] = "1970-01-01 07:14:52 GMT";
constexpr char kTime3Str[] = "1970-01-04 15:33:17 GMT";

constexpr CrashServerConfig::UploadPolicy kConfigDisabled =
    CrashServerConfig::UploadPolicy::DISABLED;
constexpr CrashServerConfig::UploadPolicy kConfigEnabled = CrashServerConfig::UploadPolicy::ENABLED;
constexpr CrashServerConfig::UploadPolicy kConfigReadFromPrivacySettings =
    CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS;

constexpr Settings::UploadPolicy kSettingsDisabled = Settings::UploadPolicy::DISABLED;
constexpr Settings::UploadPolicy kSettingsEnabled = Settings::UploadPolicy::ENABLED;
constexpr Settings::UploadPolicy kSettingsLimbo = Settings::UploadPolicy::LIMBO;

class InspectManagerTest : public testing::Test {
 public:
  void SetUp() override {
    inspector_ = std::make_unique<inspect::Inspector>();
    inspect_manager_ = std::make_unique<InspectManager>(&inspector_->GetRoot(), clock_);
  }

 protected:
  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FXL_CHECK(result.is_ok());
    return result.take_value();
  }

  timekeeper::TestClock clock_;
  std::unique_ptr<InspectManager> inspect_manager_;

 private:
  std::unique_ptr<inspect::Inspector> inspector_;
};

TEST_F(InspectManagerTest, InitialInspectTree) {
  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(NameMatches("config")),
                                 NodeMatches(NameMatches("database")),
                                 NodeMatches(NameMatches("reports")),
                                 NodeMatches(NameMatches("settings")),
                                 NodeMatches(NameMatches("queue")),
                             })));
}

TEST_F(InspectManagerTest, Succeed_AddReport_UniqueReports) {
  clock_.Set(kTime1);
  EXPECT_TRUE(inspect_manager_->AddReport("program_1", "local_report_id_1"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(
          AllOf(NodeMatches(NameMatches("reports")),
                ChildrenMatch(ElementsAre(AllOf(
                    NodeMatches(NameMatches("program_1")),
                    ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                        NameMatches("local_report_id_1"),
                        PropertyList(ElementsAre(StringIs("creation_time", kTime1Str))))))))))))));

  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program_1", "local_report_id_2"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("reports")),
          ChildrenMatch(ElementsAre(AllOf(NodeMatches(NameMatches("program_1")),
                                          ChildrenMatch(UnorderedElementsAreArray({
                                              NodeMatches(AllOf(NameMatches("local_report_id_1"),
                                                                PropertyList(ElementsAre(StringIs(
                                                                    "creation_time", kTime1Str))))),
                                              NodeMatches(AllOf(NameMatches("local_report_id_2"),
                                                                PropertyList(ElementsAre(StringIs(
                                                                    "creation_time", kTime2Str))))),
                                          })))))))));

  clock_.Set(kTime3);
  EXPECT_TRUE(inspect_manager_->AddReport("program_2", "local_report_id_3"));
  EXPECT_TRUE(inspect_manager_->AddReport("program_2", "local_report_id_4"));
  EXPECT_TRUE(inspect_manager_->AddReport("program_2", "local_report_id_5"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("reports")),
          ChildrenMatch(UnorderedElementsAreArray({
              AllOf(NodeMatches(NameMatches("program_1")),
                    ChildrenMatch(UnorderedElementsAreArray({
                        NodeMatches(
                            AllOf(NameMatches("local_report_id_1"),
                                  PropertyList(ElementsAre(StringIs("creation_time", kTime1Str))))),
                        NodeMatches(
                            AllOf(NameMatches("local_report_id_2"),
                                  PropertyList(ElementsAre(StringIs("creation_time", kTime2Str))))),
                    }))),
              AllOf(NodeMatches(NameMatches("program_2")),
                    ChildrenMatch(UnorderedElementsAreArray({
                        NodeMatches(
                            AllOf(NameMatches("local_report_id_3"),
                                  PropertyList(ElementsAre(StringIs("creation_time", kTime3Str))))),
                        NodeMatches(
                            AllOf(NameMatches("local_report_id_4"),
                                  PropertyList(ElementsAre(StringIs("creation_time", kTime3Str))))),
                        NodeMatches(
                            AllOf(NameMatches("local_report_id_5"),
                                  PropertyList(ElementsAre(StringIs("creation_time", kTime3Str))))),
                    }))),
          }))))));
}

TEST_F(InspectManagerTest, Succeed_AddReport_ProgramNameHasBackslashes) {
  const std::string program_name = "fuchsia-pkg://fuchsia.com/foo_bar.cmx";
  clock_.Set(kTime1);
  EXPECT_TRUE(inspect_manager_->AddReport(program_name, "local_report_id_1"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(
          AllOf(NodeMatches(NameMatches("reports")),
                ChildrenMatch(ElementsAre(AllOf(
                    NodeMatches(NameMatches(program_name)),
                    ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                        NameMatches("local_report_id_1"),
                        PropertyList(ElementsAre(StringIs("creation_time", kTime1Str))))))))))))));
}

TEST_F(InspectManagerTest, Fail_AddReport_DuplicateReport) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  clock_.Set(kTime3);
  EXPECT_FALSE(inspect_manager_->AddReport("program", "local_report_id"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(
          AllOf(NodeMatches(NameMatches("reports")),
                ChildrenMatch(ElementsAre(AllOf(
                    NodeMatches(NameMatches("program")),
                    ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                        NameMatches("local_report_id"),
                        PropertyList(ElementsAre(StringIs("creation_time", kTime2Str))))))))))))));
}

TEST_F(InspectManagerTest, Succeed_SetUploadAttempt) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  EXPECT_TRUE(inspect_manager_->SetUploadAttempt("local_report_id", 1u));
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("reports")),
                  ChildrenMatch(ElementsAre(AllOf(
                      NodeMatches(NameMatches("program")),
                      ChildrenMatch(ElementsAre(AllOf(NodeMatches(AllOf(
                          NameMatches("local_report_id"), PropertyList(UnorderedElementsAreArray({
                                                              StringIs("creation_time", kTime2Str),
                                                              UintIs("upload_attempts", 1u),
                                                          }))))))))))))));
}

TEST_F(InspectManagerTest, Succeed_MarkReportAsUploaded) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  clock_.Set(kTime3);
  EXPECT_TRUE(inspect_manager_->SetUploadAttempt("local_report_id", 1u));
  EXPECT_TRUE(inspect_manager_->MarkReportAsUploaded("local_report_id", "server_report_id"));
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("reports")),
                  ChildrenMatch(ElementsAre(AllOf(
                      NodeMatches(NameMatches("program")),
                      ChildrenMatch(ElementsAre(AllOf(
                          NodeMatches(AllOf(NameMatches("local_report_id"),
                                            PropertyList(UnorderedElementsAreArray({
                                                StringIs("creation_time", kTime2Str),
                                                StringIs("final_state", "uploaded"),
                                                UintIs("upload_attempts", 1u),
                                            })))),
                          ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                              NameMatches("crash_server"), PropertyList(UnorderedElementsAreArray({
                                                               StringIs("creation_time", kTime3Str),
                                                               StringIs("id", "server_report_id"),
                                                           }))))))))))))))));
}

TEST_F(InspectManagerTest, Succeed_MarkReportAsArchived) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  EXPECT_TRUE(inspect_manager_->MarkReportAsArchived("local_report_id"));
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("reports")),
                  ChildrenMatch(ElementsAre(AllOf(
                      NodeMatches(NameMatches("program")),
                      ChildrenMatch(ElementsAre(AllOf(NodeMatches(AllOf(
                          NameMatches("local_report_id"), PropertyList(UnorderedElementsAreArray({
                                                              StringIs("creation_time", kTime2Str),
                                                              StringIs("final_state", "archived"),
                                                          }))))))))))))));
}

TEST_F(InspectManagerTest, Succeed_MarkReportAsGarbageCollected) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  EXPECT_TRUE(inspect_manager_->MarkReportAsGarbageCollected("local_report_id"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("reports")),
          ChildrenMatch(ElementsAre(AllOf(
              NodeMatches(NameMatches("program")),
              ChildrenMatch(ElementsAre(AllOf(NodeMatches(AllOf(
                  NameMatches("local_report_id"), PropertyList(UnorderedElementsAreArray({
                                                      StringIs("creation_time", kTime2Str),
                                                      StringIs("final_state", "garbage_collected"),
                                                  }))))))))))))));
}

TEST_F(InspectManagerTest, Fail_SetUploadAttempt_UnknownReport) {
  EXPECT_FALSE(inspect_manager_->SetUploadAttempt("unknown_report", 1u));
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("reports")),
                                                          ChildrenMatch(IsEmpty())))));
}

TEST_F(InspectManagerTest, Fail_MarkReportAsUploaded_UnknownReport) {
  EXPECT_FALSE(inspect_manager_->MarkReportAsUploaded("unknown_report", "server_report_id"));
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("reports")),
                                                          ChildrenMatch(IsEmpty())))));
}

TEST_F(InspectManagerTest, Fail_MarkReportAsArchived_UnknownReport) {
  EXPECT_FALSE(inspect_manager_->MarkReportAsArchived("unknown_report"));
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("reports")),
                                                          ChildrenMatch(IsEmpty())))));
}

TEST_F(InspectManagerTest, Fail_MarkReportAsGarbageCollected_UnknownReport) {
  EXPECT_FALSE(inspect_manager_->MarkReportAsGarbageCollected("unknown_report"));
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("reports")),
                                                          ChildrenMatch(IsEmpty())))));
}

TEST_F(InspectManagerTest, ExposeConfig_UploadEnabled) {
  inspect_manager_->ExposeConfig(
      Config{/*crash_server=*/
             {
                 /*upload_policy=*/kConfigEnabled,
                 /*url=*/std::make_unique<std::string>("http://localhost:1234"),
             }});
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("config")),
                        ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                            NameMatches(kCrashServerKey),
                            PropertyList(UnorderedElementsAreArray({
                                StringIs(kCrashServerUploadPolicyKey, ToString(kConfigEnabled)),
                                StringIs(kCrashServerUrlKey, "http://localhost:1234"),
                            }))))))))));
}

TEST_F(InspectManagerTest, ExposeConfig_UploadDisabled) {
  inspect_manager_->ExposeConfig(Config{/*crashpad_database=*/
                                        /*crash_server=*/
                                        {
                                            /*upload_policy=*/kConfigDisabled,
                                            /*url=*/nullptr,
                                        }});
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("config")),
                        ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                            NameMatches(kCrashServerKey),
                            PropertyList(ElementsAre(StringIs(kCrashServerUploadPolicyKey,
                                                              ToString(kConfigDisabled))))))))))));
}

TEST_F(InspectManagerTest, ExposeConfig_UploadReadFromPrivacySettings) {
  inspect_manager_->ExposeConfig(Config{/*crash_server=*/
                                        {
                                            /*upload_policy=*/kConfigReadFromPrivacySettings,
                                            /*url=*/nullptr,
                                        }});
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("config")),
          ChildrenMatch(ElementsAre(NodeMatches(AllOf(
              NameMatches(kCrashServerKey),
              PropertyList(ElementsAre(StringIs(kCrashServerUploadPolicyKey,
                                                ToString(kConfigReadFromPrivacySettings))))))))))));
}

TEST_F(InspectManagerTest, ExposeSettings_TrackUploadPolicyChanges) {
  Settings settings;
  settings.set_upload_policy(kSettingsEnabled);
  inspect_manager_->ExposeSettings(&settings);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(NodeMatches(AllOf(
          NameMatches("settings"),
          PropertyList(ElementsAre(StringIs("upload_policy", ToString(kSettingsEnabled)))))))));

  settings.set_upload_policy(kSettingsDisabled);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(NodeMatches(AllOf(
          NameMatches("settings"),
          PropertyList(ElementsAre(StringIs("upload_policy", ToString(kSettingsDisabled)))))))));

  settings.set_upload_policy(kSettingsLimbo);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(NodeMatches(
          AllOf(NameMatches("settings"),
                PropertyList(ElementsAre(StringIs("upload_policy", ToString(kSettingsLimbo)))))))));

  settings.set_upload_policy(kSettingsEnabled);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(NodeMatches(AllOf(
          NameMatches("settings"),
          PropertyList(ElementsAre(StringIs("upload_policy", ToString(kSettingsEnabled)))))))));
}

TEST_F(InspectManagerTest, IncreaseReportsCleanedBy) {
  const uint64_t kNumReportsCleaned = 10;
  for (size_t i = 1; i < 5; ++i) {
    inspect_manager_->IncreaseReportsCleanedBy(kNumReportsCleaned);
    EXPECT_THAT(
        InspectTree(),
        ChildrenMatch(Contains(NodeMatches(AllOf(
            NameMatches("database"),
            PropertyList(ElementsAre(UintIs("num_reports_cleaned", i * kNumReportsCleaned))))))));
  }
}

TEST_F(InspectManagerTest, IncreaseReportsPrunedBy) {
  const uint64_t kNumReportsPruned = 10;
  for (size_t i = 1; i < 5; ++i) {
    inspect_manager_->IncreaseReportsPrunedBy(kNumReportsPruned);
    EXPECT_THAT(
        InspectTree(),
        ChildrenMatch(Contains(NodeMatches(AllOf(
            NameMatches("database"),
            PropertyList(ElementsAre(UintIs("num_reports_pruned", i * kNumReportsPruned))))))));
  }
}

TEST_F(InspectManagerTest, SetQueueSize) {
  const uint64_t kQueueSize = 10u;
  inspect_manager_->SetQueueSize(kQueueSize);
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(NodeMatches(AllOf(
                  NameMatches("queue"), PropertyList(ElementsAre(UintIs("size", kQueueSize))))))));
}

TEST_F(InspectManagerTest, Check_CanAccessMultipleReportsForTheSameProgram) {
  // A use-after-free bug was occurring when there were multiple reports for the same crashing
  // program and we would try to mark one of them as uploaded.
  // Add enough reports to force the underlying vector to resize.
  const size_t kNumReports = 150u;
  for (size_t i = 0; i < kNumReports; ++i) {
    EXPECT_TRUE(
        inspect_manager_->AddReport("program", fxl::StringPrintf("local_report_id_%zu", i)));
  }

  for (size_t i = 0; i < kNumReports; ++i) {
    EXPECT_TRUE(inspect_manager_->MarkReportAsUploaded(fxl::StringPrintf("local_report_id_%zu", i),
                                                       "server_report_id"));
  }
}

}  // namespace
}  // namespace feedback
