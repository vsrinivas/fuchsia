// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/main_service.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/main_service.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/device_id_provider.h"
#include "src/developer/forensics/testing/stubs/network_reachability_provider.h"
#include "src/developer/forensics/testing/stubs/utc_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
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
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

constexpr char kCrashServerUrl[] = "localhost:1234";

class MainServiceTest : public UnitTestFixture {
 public:
  void SetUp() override {
    Config config = {/*crash_server=*/
                     {
                         /*upload_policy=*/CrashServerConfig::UploadPolicy::ENABLED,
                         /*url=*/std::make_unique<std::string>(kCrashServerUrl),
                     }};
    info_context_ = std::make_shared<InfoContext>(&InspectRoot(), clock_, dispatcher(), services());

    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    SetUpDeviceIdProviderServer();
    SetUpNetworkReachabilityProviderServer();
    SetUpUtcProviderServer();

    main_service_ =
        MainService::TryCreate(dispatcher(), services(), clock_, info_context_, std::move(config));
    FX_CHECK(main_service_);
    RunLoopUntilIdle();
  }

 private:
  void SetUpDeviceIdProviderServer() {
    device_id_provider_server_ = std::make_unique<stubs::DeviceIdProvider>("my-device-id");
    InjectServiceProvider(device_id_provider_server_.get());
  }

  void SetUpNetworkReachabilityProviderServer() {
    network_reachability_provider_server_ = std::make_unique<stubs::NetworkReachabilityProvider>();
    InjectServiceProvider(network_reachability_provider_server_.get());
  }

  void SetUpUtcProviderServer() {
    utc_provider_server_ = std::make_unique<stubs::UtcProvider>(
        dispatcher(), std::vector<stubs::UtcProvider::Response>({stubs::UtcProvider::Response(
                          stubs::UtcProvider::Response::Value::kExternal, zx::nsec(0))}));
    InjectServiceProvider(utc_provider_server_.get());
  }

 protected:
  timekeeper::TestClock clock_;
  std::shared_ptr<InfoContext> info_context_;

  // Stubs servers.
  std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server_;
  std::unique_ptr<stubs::NetworkReachabilityProvider> network_reachability_provider_server_;
  std::unique_ptr<stubs::UtcProviderBase> utc_provider_server_;

 protected:
  std::unique_ptr<MainService> main_service_;
};

TEST_F(MainServiceTest, Check_InitialInspectTree) {
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("config")),
                ChildrenMatch(ElementsAre(NodeMatches(
                    AllOf(NameMatches(kCrashServerKey),
                          PropertyList(UnorderedElementsAreArray({
                              StringIs(kCrashServerUploadPolicyKey,
                                       ToString(CrashServerConfig::UploadPolicy::ENABLED)),
                              StringIs(kCrashServerUrlKey, kCrashServerUrl),
                          }))))))),
          AllOf(NodeMatches(NameMatches("crash_reporter")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("store"),
                                      PropertyList(ElementsAre(
                                          UintIs("max_size_in_kb", kStoreMaxSize.ToKilobytes()))))),
                    NodeMatches(
                        AllOf(NameMatches("settings"),
                              PropertyList(ElementsAre(StringIs(
                                  "upload_policy", ToString(Settings::UploadPolicy::ENABLED)))))),
                    NodeMatches(NameMatches("reports")),
                    NodeMatches(NameMatches("queue")),
                }))),
          AllOf(NodeMatches(NameMatches("fidl")),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReporter"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("current_num_connections", 0u),
                                          UintIs("total_num_connections", 0u),
                                      })))),
                    NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReportingProductRegister"),
                                      PropertyList(UnorderedElementsAreArray({
                                          UintIs("current_num_connections", 0u),
                                          UintIs("total_num_connections", 0u),
                                      })))),
                }))))));
}

TEST_F(MainServiceTest, CrashReporter_CheckInspect) {
  const size_t kNumConnections = 4;
  fuchsia::feedback::CrashReporterSyncPtr crash_reporters[kNumConnections];

  // Add 3 new connections.
  main_service_->HandleCrashReporterRequest(crash_reporters[0].NewRequest());
  main_service_->HandleCrashReporterRequest(crash_reporters[1].NewRequest());
  main_service_->HandleCrashReporterRequest(crash_reporters[2].NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReporter"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("current_num_connections", 3u),
                                                       UintIs("total_num_connections", 3u),
                                                   }))))))))));

  // Close 1 connection.
  crash_reporters[1].Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReporter"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("current_num_connections", 2u),
                                                       UintIs("total_num_connections", 3u),
                                                   }))))))))));

  // Add 1 new connection.
  main_service_->HandleCrashReporterRequest(crash_reporters[3].NewRequest());
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReporter"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("current_num_connections", 3u),
                                                       UintIs("total_num_connections", 4u),
                                                   }))))))))));

  // Close remaining connections.
  crash_reporters[0].Unbind();
  crash_reporters[2].Unbind();
  crash_reporters[3].Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("fidl")),
          ChildrenMatch(Contains(NodeMatches(AllOf(NameMatches("fuchsia.feedback.CrashReporter"),
                                                   PropertyList(UnorderedElementsAreArray({
                                                       UintIs("current_num_connections", 0u),
                                                       UintIs("total_num_connections", 4u),
                                                   }))))))))));
}

TEST_F(MainServiceTest, CrashRegister_CheckInspect) {
  const size_t kNumConnections = 4;
  fuchsia::feedback::CrashReportingProductRegisterSyncPtr crash_registers[kNumConnections];

  // Add 3 new connections.
  main_service_->HandleCrashRegisterRequest(crash_registers[0].NewRequest());
  main_service_->HandleCrashRegisterRequest(crash_registers[1].NewRequest());
  main_service_->HandleCrashRegisterRequest(crash_registers[2].NewRequest());
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(
                                 NodeMatches(NameMatches("fidl")),
                                 ChildrenMatch(Contains(NodeMatches(AllOf(
                                     NameMatches("fuchsia.feedback.CrashReportingProductRegister"),
                                     PropertyList(UnorderedElementsAreArray({
                                         UintIs("current_num_connections", 3u),
                                         UintIs("total_num_connections", 3u),
                                     }))))))))));

  // Close 1 connection.
  crash_registers[1].Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(
                                 NodeMatches(NameMatches("fidl")),
                                 ChildrenMatch(Contains(NodeMatches(AllOf(
                                     NameMatches("fuchsia.feedback.CrashReportingProductRegister"),
                                     PropertyList(UnorderedElementsAreArray({
                                         UintIs("current_num_connections", 2u),
                                         UintIs("total_num_connections", 3u),
                                     }))))))))));

  // Add 1 new connection.
  main_service_->HandleCrashRegisterRequest(crash_registers[3].NewRequest());
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(
                                 NodeMatches(NameMatches("fidl")),
                                 ChildrenMatch(Contains(NodeMatches(AllOf(
                                     NameMatches("fuchsia.feedback.CrashReportingProductRegister"),
                                     PropertyList(UnorderedElementsAreArray({
                                         UintIs("current_num_connections", 3u),
                                         UintIs("total_num_connections", 4u),
                                     }))))))))));

  // Close remaining connections.
  crash_registers[0].Unbind();
  crash_registers[2].Unbind();
  crash_registers[3].Unbind();
  RunLoopUntilIdle();
  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(
                                 NodeMatches(NameMatches("fidl")),
                                 ChildrenMatch(Contains(NodeMatches(AllOf(
                                     NameMatches("fuchsia.feedback.CrashReportingProductRegister"),
                                     PropertyList(UnorderedElementsAreArray({
                                         UintIs("current_num_connections", 0u),
                                         UintIs("total_num_connections", 4u),
                                     }))))))))));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
