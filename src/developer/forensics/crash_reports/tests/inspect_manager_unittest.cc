// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/info/inspect_manager.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <cstdint>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/errors.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/crash_reports/settings.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"
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
using testing::Not;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

constexpr char kComponentUrl[] = "fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx";

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

class InspectManagerTest : public UnitTestFixture {
 public:
  void SetUp() override {
    inspect_manager_ = std::make_unique<InspectManager>(&InspectRoot(), &clock_);
  }

 protected:
  timekeeper::TestClock clock_;
  std::unique_ptr<InspectManager> inspect_manager_;
};

TEST_F(InspectManagerTest, InitialInspectTree) {
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("config")),
                                                 AllOf(NodeMatches(NameMatches("crash_reporter")),
                                                       ChildrenMatch(UnorderedElementsAreArray({
                                                           NodeMatches(NameMatches("queue")),
                                                           NodeMatches(NameMatches("reports")),
                                                           NodeMatches(NameMatches("settings")),
                                                       }))),
                                                 NodeMatches(NameMatches("fidl")))));
}

TEST_F(InspectManagerTest, Succeed_AddReport_UniqueReports) {
  clock_.Set(kTime1);
  EXPECT_TRUE(inspect_manager_->AddReport("program_1", "local_report_id_1"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(AllOf(
              NodeMatches(NameMatches("reports")),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(NameMatches("program_1")),
                  ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                      NameMatches("local_report_id_1"),
                      PropertyList(ElementsAre(StringIs("creation_time", kTime1Str)))))))))))))))));

  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program_1", "local_report_id_2"));
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("crash_reporter")),
                  ChildrenMatch(Contains(AllOf(
                      NodeMatches(NameMatches("reports")),
                      ChildrenMatch(ElementsAre(AllOf(
                          NodeMatches(NameMatches("program_1")),
                          ChildrenMatch(UnorderedElementsAreArray({
                              NodeMatches(AllOf(
                                  NameMatches("local_report_id_1"),
                                  PropertyList(ElementsAre(StringIs("creation_time", kTime1Str))))),
                              NodeMatches(AllOf(
                                  NameMatches("local_report_id_2"),
                                  PropertyList(ElementsAre(StringIs("creation_time", kTime2Str))))),
                          }))))))))))));

  clock_.Set(kTime3);
  EXPECT_TRUE(inspect_manager_->AddReport("program_2", "local_report_id_3"));
  EXPECT_TRUE(inspect_manager_->AddReport("program_2", "local_report_id_4"));
  EXPECT_TRUE(inspect_manager_->AddReport("program_2", "local_report_id_5"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(AllOf(
              NodeMatches(NameMatches("reports")),
              ChildrenMatch(UnorderedElementsAreArray({
                  AllOf(NodeMatches(NameMatches("program_1")),
                        ChildrenMatch(UnorderedElementsAreArray({
                            NodeMatches(AllOf(
                                NameMatches("local_report_id_1"),
                                PropertyList(ElementsAre(StringIs("creation_time", kTime1Str))))),
                            NodeMatches(AllOf(
                                NameMatches("local_report_id_2"),
                                PropertyList(ElementsAre(StringIs("creation_time", kTime2Str))))),
                        }))),
                  AllOf(NodeMatches(NameMatches("program_2")),
                        ChildrenMatch(UnorderedElementsAreArray({
                            NodeMatches(AllOf(
                                NameMatches("local_report_id_3"),
                                PropertyList(ElementsAre(StringIs("creation_time", kTime3Str))))),
                            NodeMatches(AllOf(
                                NameMatches("local_report_id_4"),
                                PropertyList(ElementsAre(StringIs("creation_time", kTime3Str))))),
                            NodeMatches(AllOf(
                                NameMatches("local_report_id_5"),
                                PropertyList(ElementsAre(StringIs("creation_time", kTime3Str))))),
                        }))),
              })))))))));
}

TEST_F(InspectManagerTest, Succeed_AddReport_ProgramNameHasBackslashes) {
  clock_.Set(kTime1);
  EXPECT_TRUE(inspect_manager_->AddReport(kComponentUrl, "local_report_id_1"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(AllOf(
              NodeMatches(NameMatches("reports")),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(NameMatches(kComponentUrl)),
                  ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                      NameMatches("local_report_id_1"),
                      PropertyList(ElementsAre(StringIs("creation_time", kTime1Str)))))))))))))))));
}

TEST_F(InspectManagerTest, Fail_AddReport_DuplicateReport) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  clock_.Set(kTime3);
  EXPECT_FALSE(inspect_manager_->AddReport("program", "local_report_id"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(AllOf(
              NodeMatches(NameMatches("reports")),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(NameMatches("program")),
                  ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                      NameMatches("local_report_id"),
                      PropertyList(ElementsAre(StringIs("creation_time", kTime2Str)))))))))))))))));
}

TEST_F(InspectManagerTest, Succeed_SetUploadAttempt) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  EXPECT_TRUE(inspect_manager_->SetUploadAttempt("local_report_id", 1u));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(AllOf(
              NodeMatches(NameMatches("reports")),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(NameMatches("program")),
                  ChildrenMatch(ElementsAre(AllOf(NodeMatches(AllOf(
                      NameMatches("local_report_id"), PropertyList(UnorderedElementsAreArray({
                                                          StringIs("creation_time", kTime2Str),
                                                          UintIs("upload_attempts", 1u),
                                                      })))))))))))))))));
}

TEST_F(InspectManagerTest, Succeed_MarkReportAsUploaded) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  clock_.Set(kTime3);
  EXPECT_TRUE(inspect_manager_->SetUploadAttempt("local_report_id", 1u));
  EXPECT_TRUE(inspect_manager_->MarkReportAsUploaded("local_report_id", "server_report_id"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
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
                                                       })))))))))))))))))));
}

TEST_F(InspectManagerTest, Succeed_MarkReportAsArchived) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  EXPECT_TRUE(inspect_manager_->MarkReportAsArchived("local_report_id"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(AllOf(
              NodeMatches(NameMatches("reports")),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(NameMatches("program")),
                  ChildrenMatch(ElementsAre(AllOf(NodeMatches(AllOf(
                      NameMatches("local_report_id"), PropertyList(UnorderedElementsAreArray({
                                                          StringIs("creation_time", kTime2Str),
                                                          StringIs("final_state", "archived"),
                                                      })))))))))))))))));
}

TEST_F(InspectManagerTest, Succeed_MarkReportAsGarbageCollected) {
  clock_.Set(kTime2);
  EXPECT_TRUE(inspect_manager_->AddReport("program", "local_report_id"));
  EXPECT_TRUE(inspect_manager_->MarkReportAsGarbageCollected("local_report_id"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(AllOf(
              NodeMatches(NameMatches("reports")),
              ChildrenMatch(ElementsAre(AllOf(NodeMatches(NameMatches("program")),
                                              ChildrenMatch(ElementsAre(AllOf(NodeMatches(AllOf(
                                                  NameMatches("local_report_id"),
                                                  PropertyList(UnorderedElementsAreArray({
                                                      StringIs("creation_time", kTime2Str),
                                                      StringIs("final_state", "garbage_collected"),
                                                  })))))))))))))))));
}

TEST_F(InspectManagerTest, Fail_SetUploadAttempt_UnknownReport) {
  EXPECT_FALSE(inspect_manager_->SetUploadAttempt("unknown_report", 1u));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("crash_reporter")),
                                   ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("reports")),
                                                                ChildrenMatch(IsEmpty()))))))));
}

TEST_F(InspectManagerTest, Fail_MarkReportAsUploaded_UnknownReport) {
  EXPECT_FALSE(inspect_manager_->MarkReportAsUploaded("unknown_report", "server_report_id"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("crash_reporter")),
                                   ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("reports")),
                                                                ChildrenMatch(IsEmpty()))))))));
}

TEST_F(InspectManagerTest, Fail_MarkReportAsArchived_UnknownReport) {
  EXPECT_FALSE(inspect_manager_->MarkReportAsArchived("unknown_report"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("crash_reporter")),
                                   ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("reports")),
                                                                ChildrenMatch(IsEmpty()))))))));
}

TEST_F(InspectManagerTest, Fail_MarkReportAsGarbageCollected_UnknownReport) {
  EXPECT_FALSE(inspect_manager_->MarkReportAsGarbageCollected("unknown_report"));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("crash_reporter")),
                                   ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("reports")),
                                                                ChildrenMatch(IsEmpty()))))))));
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
  inspect_manager_->ExposeConfig(Config{/*crash_server=*/
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
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(NodeMatches(AllOf(
              NameMatches("settings"), PropertyList(ElementsAre(StringIs(
                                           "upload_policy", ToString(kSettingsEnabled))))))))))));

  settings.set_upload_policy(kSettingsDisabled);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(NodeMatches(AllOf(
              NameMatches("settings"), PropertyList(ElementsAre(StringIs(
                                           "upload_policy", ToString(kSettingsDisabled))))))))))));

  settings.set_upload_policy(kSettingsLimbo);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(NodeMatches(AllOf(
              NameMatches("settings"), PropertyList(ElementsAre(StringIs(
                                           "upload_policy", ToString(kSettingsLimbo))))))))))));

  settings.set_upload_policy(kSettingsEnabled);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(NodeMatches(AllOf(
              NameMatches("settings"), PropertyList(ElementsAre(StringIs(
                                           "upload_policy", ToString(kSettingsEnabled))))))))))));
}

TEST_F(InspectManagerTest, IncreaseReportsGarbageCollectedBy) {
  const uint64_t kNumReportsGarbageCollected = 10;
  for (size_t i = 1; i < 5; ++i) {
    inspect_manager_->IncreaseReportsGarbageCollectedBy(kNumReportsGarbageCollected);
    EXPECT_THAT(InspectTree(),
                ChildrenMatch(Contains(AllOf(
                    NodeMatches(NameMatches("crash_reporter")),
                    ChildrenMatch(Contains(NodeMatches(AllOf(
                        NameMatches("store"),
                        PropertyList(ElementsAre(UintIs("num_reports_garbage_collected",
                                                        i * kNumReportsGarbageCollected)))))))))));
  }
}

TEST_F(InspectManagerTest, SetQueueSize) {
  const uint64_t kQueueSize = 10u;
  inspect_manager_->SetQueueSize(kQueueSize);
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("crash_reporter")),
          ChildrenMatch(Contains(NodeMatches(AllOf(
              NameMatches("queue"), PropertyList(ElementsAre(UintIs("size", kQueueSize)))))))))));
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

TEST_F(InspectManagerTest, UpsertComponentToProductMapping) {
  // 1. We insert a product with all the fields set.
  const Product product{.name = "some name",
                        .version = ErrorOr<std::string>("some version"),
                        .channel = ErrorOr<std::string>("some channel")};
  inspect_manager_->UpsertComponentToProductMapping(kComponentUrl, product);
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(
                                 NodeMatches(NameMatches("crash_register")),
                                 ChildrenMatch(Contains(AllOf(
                                     NodeMatches(NameMatches("mappings")),
                                     ChildrenMatch(UnorderedElementsAreArray({
                                         NodeMatches(AllOf(NameMatches(kComponentUrl),
                                                           PropertyList(UnorderedElementsAreArray({
                                                               StringIs("name", "some name"),
                                                               StringIs("version", "some version"),
                                                               StringIs("channel", "some channel"),
                                                           })))),
                                     })))))))));

  // 2. We insert the same product under a different component URL.
  const std::string another_component_url = std::string(kComponentUrl) + "2";
  inspect_manager_->UpsertComponentToProductMapping(another_component_url, product);
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(
                                 NodeMatches(NameMatches("crash_register")),
                                 ChildrenMatch(Contains(AllOf(
                                     NodeMatches(NameMatches("mappings")),
                                     ChildrenMatch(UnorderedElementsAreArray({
                                         NodeMatches(AllOf(NameMatches(kComponentUrl),
                                                           PropertyList(UnorderedElementsAreArray({
                                                               StringIs("name", "some name"),
                                                               StringIs("version", "some version"),
                                                               StringIs("channel", "some channel"),
                                                           })))),
                                         NodeMatches(AllOf(NameMatches(another_component_url),
                                                           PropertyList(UnorderedElementsAreArray({
                                                               StringIs("name", "some name"),
                                                               StringIs("version", "some version"),
                                                               StringIs("channel", "some channel"),
                                                           })))),
                                     })))))))));

  // 3. We update the product under the first component URL with some missing fields.
  const Product another_product{.name = "some other name",
                                .version = ErrorOr<std::string>(Error::kMissingValue),
                                .channel = ErrorOr<std::string>(Error::kMissingValue)};
  inspect_manager_->UpsertComponentToProductMapping(kComponentUrl, another_product);
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("crash_register")),
                  ChildrenMatch(Contains(AllOf(
                      NodeMatches(NameMatches("mappings")),
                      ChildrenMatch(UnorderedElementsAreArray({
                          NodeMatches(AllOf(NameMatches(kComponentUrl),
                                            PropertyList(UnorderedElementsAreArray({
                                                StringIs("name", "some other name"),
                                                StringIs("version", ToReason(Error::kMissingValue)),
                                                StringIs("channel", ToReason(Error::kMissingValue)),
                                            })))),
                          NodeMatches(AllOf(NameMatches(another_component_url),
                                            PropertyList(UnorderedElementsAreArray({
                                                StringIs("name", "some name"),
                                                StringIs("version", "some version"),
                                                StringIs("channel", "some channel"),
                                            })))),
                      })))))))));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
