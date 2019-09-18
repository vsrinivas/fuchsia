// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/inspect_manager.h"

#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>

#include "sdk/lib/inspect/testing/cpp/inspect.h"
#include "src/developer/feedback/crashpad_agent/constants.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
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

class InspectManagerTest : public testing::Test {
 public:
  void SetUp() override {
    inspector_ = std::make_unique<inspect::Inspector>();
    inspect_manager_ = std::make_unique<InspectManager>(&inspector_->GetRoot());
  }

 protected:
  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FXL_CHECK(result.is_ok());
    return result.take_value();
  }

  std::unique_ptr<InspectManager> inspect_manager_;

 private:
  std::unique_ptr<inspect::Inspector> inspector_;
};

TEST_F(InspectManagerTest, InitialInspectTree) {
  EXPECT_THAT(InspectTree(), ChildrenMatch(UnorderedElementsAreArray({
                                 NodeMatches(NameMatches(kInspectConfigName)),
                                 NodeMatches(NameMatches(kInspectReportsName)),
                             })));
}

TEST_F(InspectManagerTest, AddReport) {
  inspect_manager_->AddReport("program_1", "local_report_id_1");
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches(kInspectReportsName)),
          ChildrenMatch(ElementsAre(AllOf(
              NodeMatches(NameMatches("program_1")),
              ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                  NameMatches("local_report_id_1"),
                  PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty())))))))))))))));

  inspect_manager_->AddReport("program_1", "local_report_id_2");
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches(kInspectReportsName)),
          ChildrenMatch(ElementsAre(AllOf(
              NodeMatches(NameMatches("program_1")),
              ChildrenMatch(UnorderedElementsAreArray({
                  NodeMatches(
                      AllOf(NameMatches("local_report_id_1"),
                            PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty())))))),
                  NodeMatches(
                      AllOf(NameMatches("local_report_id_2"),
                            PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty())))))),
              })))))))));

  inspect_manager_->AddReport("program_2", "local_report_id_1");
  inspect_manager_->AddReport("program_2", "local_report_id_2");
  inspect_manager_->AddReport("program_2", "local_report_id_3");
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches(kInspectReportsName)),
          ChildrenMatch(UnorderedElementsAreArray({
              AllOf(NodeMatches(NameMatches("program_1")),
                    ChildrenMatch(UnorderedElementsAreArray({
                        NodeMatches(AllOf(
                            NameMatches("local_report_id_1"),
                            PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty())))))),
                        NodeMatches(AllOf(
                            NameMatches("local_report_id_2"),
                            PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty())))))),
                    }))),
              AllOf(NodeMatches(NameMatches("program_2")),
                    ChildrenMatch(UnorderedElementsAreArray({
                        NodeMatches(AllOf(
                            NameMatches("local_report_id_1"),
                            PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty())))))),
                        NodeMatches(AllOf(
                            NameMatches("local_report_id_2"),
                            PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty())))))),
                        NodeMatches(AllOf(
                            NameMatches("local_report_id_3"),
                            PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty())))))),
                    }))),
          }))))));
}

TEST_F(InspectManagerTest, MarkAsUploaded) {
  InspectManager::Report* report = inspect_manager_->AddReport("program_1", "local_report_id_1");
  report->MarkAsUploaded("server_report_id_1");
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches(kInspectReportsName)),
          ChildrenMatch(ElementsAre(AllOf(
              NodeMatches(NameMatches("program_1")),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(AllOf(
                      NameMatches("local_report_id_1"),
                      PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty())))))),
                  ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                      NameMatches("crash_server"), PropertyList(UnorderedElementsAreArray({
                                                       StringIs("creation_time", Not(IsEmpty())),
                                                       StringIs("id", "server_report_id_1"),
                                                   }))))))))))))))));
}

TEST_F(InspectManagerTest, ExposeConfig_UploadEnabled) {
  inspect_manager_->ExposeConfig(Config{
      /*crashpad_database=*/
      {
          /*path=*/"/foo/crashes",
          /*max_size_in_kb=*/1234,
      },
      /*crash_server=*/
      {
          /*enable_upload=*/true,
          /*url=*/std::make_unique<std::string>("http://localhost:1234"),
      },
      /*feedback_data_collection_timeout=*/zx::msec(10)});
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(AllOf(
              NameMatches(kInspectConfigName),
              PropertyList(Contains(UintIs(kFeedbackDataCollectionTimeoutInMillisecondsKey, 10))))),
          ChildrenMatch(UnorderedElementsAreArray({
              NodeMatches(AllOf(NameMatches(kCrashpadDatabaseKey),
                                PropertyList(UnorderedElementsAreArray({
                                    StringIs(kCrashpadDatabasePathKey, "/foo/crashes"),
                                    UintIs(kCrashpadDatabaseMaxSizeInKbKey, 1234),
                                })))),
              NodeMatches(AllOf(NameMatches(kCrashServerKey),
                                PropertyList(UnorderedElementsAreArray({
                                    StringIs(kCrashServerEnableUploadKey, "true"),
                                    StringIs(kCrashServerUrlKey, "http://localhost:1234"),
                                })))),
          }))))));
}

TEST_F(InspectManagerTest, ExposeConfig_UploadDisabled) {
  inspect_manager_->ExposeConfig(Config{/*crashpad_database=*/
                                        {
                                            /*path=*/"/foo/crashes",
                                            /*max_size_in_kb=*/1234,
                                        },
                                        /*crash_server=*/
                                        {
                                            /*enable_upload=*/false,
                                            /*url=*/nullptr,
                                        },
                                        /*feedback_data_collection_timeout=*/zx::msec(10)});
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(AllOf(
              NameMatches(kInspectConfigName),
              PropertyList(Contains(UintIs(kFeedbackDataCollectionTimeoutInMillisecondsKey, 10))))),
          ChildrenMatch(UnorderedElementsAreArray({
              NodeMatches(AllOf(NameMatches(kCrashpadDatabaseKey),
                                PropertyList(UnorderedElementsAreArray({
                                    StringIs(kCrashpadDatabasePathKey, "/foo/crashes"),
                                    UintIs(kCrashpadDatabaseMaxSizeInKbKey, 1234),
                                })))),
              NodeMatches(
                  AllOf(NameMatches(kCrashServerKey),
                        PropertyList(ElementsAre(StringIs(kCrashServerEnableUploadKey, "false"))))),
          }))))));
}

}  // namespace
}  // namespace feedback

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
