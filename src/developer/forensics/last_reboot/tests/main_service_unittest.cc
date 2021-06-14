// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/last_reboot/main_service.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/cobalt_logger.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/reboot_methods_watcher_register.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace last_reboot {
namespace {

using fuchsia::feedback::LastRebootInfoProviderSyncPtr;
using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using testing::Contains;
using testing::UnorderedElementsAreArray;

class MainServiceTest : public UnitTestFixture {
 public:
  MainServiceTest()
      : main_service_(MainService::Config{
            .dispatcher = dispatcher(),
            .services = services(),
            .clock = &clock_,
            .root_node = &InspectRoot(),
            .reboot_log =
                feedback::RebootLog(feedback::RebootReason::kNotParseable, "", std::nullopt),
            .graceful_reboot_reason_write_path = Path(),
        }) {}

 protected:
  void SetUpRebootMethodsWatcherRegisterServer(
      std::unique_ptr<stubs::RebootMethodsWatcherRegisterBase> server) {
    reboot_watcher_register_server_ = std::move(server);
    if (reboot_watcher_register_server_) {
      InjectServiceProvider(reboot_watcher_register_server_.get());
    }
  }

  std::string Path() { return files::JoinPath(tmp_dir_.path(), "graceful_reboot_reason.txt"); }

 private:
  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<stubs::RebootMethodsWatcherRegisterBase> reboot_watcher_register_server_;

 protected:
  timekeeper::TestClock clock_;
  MainService main_service_;
};

TEST_F(MainServiceTest, Check_RegistersRebootWatcher) {
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
  SetUpRebootMethodsWatcherRegisterServer(std::make_unique<stubs::RebootMethodsWatcherRegister>(
      fuchsia::hardware::power::statecontrol::RebootReason::USER_REQUEST));
  RunLoopUntilIdle();

  main_service_.WatchForImminentGracefulReboot();
  RunLoopUntilIdle();

  EXPECT_TRUE(files::IsFile(Path()));

  std::string reboot_reason_str;
  ASSERT_TRUE(files::ReadFileToString(Path(), &reboot_reason_str));
  EXPECT_EQ(reboot_reason_str, "USER REQUEST");
}

TEST_F(MainServiceTest, CheckInspect) {
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 0u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                }))),
      })));
}

TEST_F(MainServiceTest, LastRebootInfoProvider_CheckInspect) {
  SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
  LastRebootInfoProviderSyncPtr last_reboot_info_provider_1;
  main_service_.HandleLastRebootInfoProviderRequest(last_reboot_info_provider_1.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 1u),
                                          UintIs("current_num_connections", 1u),
                                      })))),
                }))),
      })));

  LastRebootInfoProviderSyncPtr last_reboot_info_provider_2;
  main_service_.HandleLastRebootInfoProviderRequest(last_reboot_info_provider_2.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 2u),
                                          UintIs("current_num_connections", 2u),
                                      })))),
                }))),
      })));

  last_reboot_info_provider_1.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 2u),
                                          UintIs("current_num_connections", 1u),
                                      })))),
                }))),
      })));

  LastRebootInfoProviderSyncPtr last_reboot_info_provider_3;
  main_service_.HandleLastRebootInfoProviderRequest(last_reboot_info_provider_3.NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 3u),
                                          UintIs("current_num_connections", 2u),
                                      })))),
                }))),
      })));

  last_reboot_info_provider_2.Unbind();
  last_reboot_info_provider_3.Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAreArray({
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.LastRebootInfoProvider"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("total_num_connections", 3u),
                                          UintIs("current_num_connections", 0u),
                                      })))),
                }))),
      })));
}

}  // namespace
}  // namespace last_reboot
}  // namespace forensics
