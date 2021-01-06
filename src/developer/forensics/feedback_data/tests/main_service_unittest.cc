// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/main_service.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/logger.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"
#include "src/developer/forensics/testing/log_message.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::ComponentDataRegisterSyncPtr;
using fuchsia::feedback::DataProviderSyncPtr;
using fuchsia::feedback::DeviceIdProviderSyncPtr;
using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using testing::BuildLogMessage;
using ::testing::Contains;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAreArray;

std::string MakeFilepath(const std::string& dir, const size_t file_num) {
  return files::JoinPath(dir, std::to_string(file_num));
}

const std::vector<std::string> kCurrentLogFilePaths = {
    MakeFilepath(kCurrentLogsDir, 0), MakeFilepath(kCurrentLogsDir, 1),
    MakeFilepath(kCurrentLogsDir, 2), MakeFilepath(kCurrentLogsDir, 3),
    MakeFilepath(kCurrentLogsDir, 4), MakeFilepath(kCurrentLogsDir, 5),
    MakeFilepath(kCurrentLogsDir, 6), MakeFilepath(kCurrentLogsDir, 7),
};

class MainServiceTest : public UnitTestFixture {
 public:
  void SetUp() {
    FX_CHECK(files::CreateDirectory(kCurrentLogsDir));
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    RunLoopUntilIdle();
  }

  void TearDown() override {
    FX_CHECK(files::DeletePath(kPreviousLogsFilePath, /*recursive=*/true));
    FX_CHECK(files::DeletePath(kCurrentLogsDir, /*recursive=*/true));
  }

 protected:
  void CreateMainService(const bool is_first_instance) {
    main_service_ =
        MainService::TryCreate(dispatcher(), services(), &InspectRoot(), is_first_instance);
  }

  void WriteFile(const std::string& filepath, const std::string& content) {
    FX_CHECK(files::WriteFile(filepath, content.c_str(), content.size()));
  }

  std::string ReadFile(const std::string& filepath) {
    std::string content;
    FX_CHECK(files::ReadFileToString(filepath, &content));
    return content;
  }

  std::unique_ptr<MainService> main_service_;
};

MATCHER_P2(MatchesCobaltEvent, expected_type, expected_metric_id, "") {
  return arg.type == expected_type && arg.metric_id == expected_metric_id;
}

TEST_F(MainServiceTest, MovesPreviousBootLogs) {
  std::string previous_log_contents = "";
  for (const auto& filepath : kCurrentLogFilePaths) {
    auto encoder = system_log_recorder::ProductionEncoder();
    const std::string str = Format(BuildLogMessage(FX_LOG_INFO, "Log for file: " + filepath));
    previous_log_contents = previous_log_contents + str;
    WriteFile(filepath, encoder.Encode(str));
  }

  CreateMainService(/*is_first_instance=*/true);
  RunLoopUntilIdle();

  EXPECT_FALSE(files::IsDirectory(kCurrentLogsDir));
  EXPECT_EQ(previous_log_contents, ReadFile(kPreviousLogsFilePath));

  // Verify the event type and metric_id.
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  MatchesCobaltEvent(cobalt::EventType::kCount,
                                     cobalt_registry::kPreviousBootLogCompressionRatioMetricId),
              }));
}

TEST_F(MainServiceTest, NoMovesPreviousBootLogsAfterFirstInstance) {
  std::string previous_log_contents = "";
  for (const auto& filepath : kCurrentLogFilePaths) {
    auto encoder = system_log_recorder::ProductionEncoder();
    const std::string str = Format(BuildLogMessage(FX_LOG_INFO, "Log for file: " + filepath));
    previous_log_contents = previous_log_contents + str;
    WriteFile(filepath, encoder.Encode(str));
  }

  CreateMainService(/*is_first_instance=*/false);
  RunLoopUntilIdle();

  EXPECT_TRUE(files::IsDirectory(kCurrentLogsDir));
  // We check that nothing has been moved to /tmp.
  EXPECT_FALSE(files::IsFile(kPreviousLogsFilePath));

  // We check that the content of /cache is still the same.
  for (const auto& filepath : kCurrentLogFilePaths) {
    auto encoder = system_log_recorder::ProductionEncoder();
    const std::string str = Format(BuildLogMessage(FX_LOG_INFO, "Log for file: " + filepath));
    EXPECT_EQ(encoder.Encode(str), ReadFile(filepath));
  }

  // Verify no event was sent to cobalt.
  EXPECT_THAT(ReceivedCobaltEvents(), IsEmpty());
}

TEST_F(MainServiceTest, CheckInspect) {
  CreateMainService(/*is_first_instance=*/true);
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(UnorderedElementsAreArray({
                  AllOf(NodeMatches(NameMatches("fidl")),
                        ChildrenMatch(UnorderedElementsAreArray({
                            NodeMatches(AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                              PropertyList(UnorderedElementsAreArray({
                                                  UintIs("total_num_connections", 0u),
                                                  UintIs("current_num_connections", 0u),
                                              })))),
                            NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                              PropertyList(UnorderedElementsAreArray({
                                                  UintIs("total_num_connections", 0u),
                                                  UintIs("current_num_connections", 0u),
                                              })))),
                            NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                              PropertyList(UnorderedElementsAreArray({
                                                  UintIs("total_num_connections", 0u),
                                                  UintIs("current_num_connections", 0u),
                                              })))),
                        }))),
              })));
}

TEST_F(MainServiceTest, ComponentDataRegister_CheckInspect) {
  CreateMainService(/*is_first_instance=*/true);
  ComponentDataRegisterSyncPtr data_register_1;
  main_service_->HandleComponentDataRegisterRequest(data_register_1.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 1u),
                                                 UintIs("current_num_connections", 1u),
                                             }))))))))));

  ComponentDataRegisterSyncPtr data_register_2;
  main_service_->HandleComponentDataRegisterRequest(data_register_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 2u),
                                                 UintIs("current_num_connections", 2u),
                                             }))))))))));

  data_register_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 2u),
                                                 UintIs("current_num_connections", 1u),
                                             }))))))))));

  ComponentDataRegisterSyncPtr data_register_3;
  main_service_->HandleComponentDataRegisterRequest(data_register_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 3u),
                                                 UintIs("current_num_connections", 2u),
                                             }))))))))));

  data_register_2.Unbind();
  data_register_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(NodeMatches(NameMatches("fidl")),
                                   ChildrenMatch(Contains(NodeMatches(
                                       AllOf(NameMatches("fuchsia.feedback.ComponentDataRegister"),
                                             PropertyList(UnorderedElementsAreArray({
                                                 UintIs("total_num_connections", 3u),
                                                 UintIs("current_num_connections", 0u),
                                             }))))))))));
}

TEST_F(MainServiceTest, DataProvider_CheckInspect) {
  CreateMainService(/*is_first_instance=*/true);
  DataProviderSyncPtr data_provider_1;
  main_service_->HandleDataProviderRequest(data_provider_1.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 1u),
                                                       UintIs("current_num_connections", 1u),
                                                   }))))))))));

  DataProviderSyncPtr data_provider_2;
  main_service_->HandleDataProviderRequest(data_provider_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 2u),
                                                       UintIs("current_num_connections", 2u),
                                                   }))))))))));

  data_provider_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 2u),
                                                       UintIs("current_num_connections", 1u),
                                                   }))))))))));

  DataProviderSyncPtr data_provider_3;
  main_service_->HandleDataProviderRequest(data_provider_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 3u),
                                                       UintIs("current_num_connections", 2u),
                                                   }))))))))));

  data_provider_2.Unbind();
  data_provider_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DataProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 3u),
                                                       UintIs("current_num_connections", 0u),
                                                   }))))))))));
}

TEST_F(MainServiceTest, DeviceIdProvider_CheckInspect) {
  CreateMainService(/*is_first_instance=*/true);
  DeviceIdProviderSyncPtr device_id_provider_1;
  main_service_->HandleDeviceIdProviderRequest(device_id_provider_1.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 1u),
                                                       UintIs("current_num_connections", 1u),
                                                   }))))))))));

  DeviceIdProviderSyncPtr device_id_provider_2;
  main_service_->HandleDeviceIdProviderRequest(device_id_provider_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 2u),
                                                       UintIs("current_num_connections", 2u),
                                                   }))))))))));

  device_id_provider_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 2u),
                                                       UintIs("current_num_connections", 1u),
                                                   }))))))))));

  DeviceIdProviderSyncPtr device_id_provider_3;
  main_service_->HandleDeviceIdProviderRequest(device_id_provider_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 3u),
                                                       UintIs("current_num_connections", 2u),
                                                   }))))))))));

  device_id_provider_2.Unbind();
  device_id_provider_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.DeviceIdProvider"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("total_num_connections", 3u),
                                                       UintIs("current_num_connections", 0u),
                                                   }))))))))));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
