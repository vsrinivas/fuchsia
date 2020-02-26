// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_app.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/cobalt/bin/app/testapp_metrics_registry.cb.h"
#include "src/cobalt/bin/testapp/fake_timekeeper.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/network_wrapper/fake_network_wrapper.h"
#include "third_party/abseil-cpp/absl/strings/match.h"
#include "third_party/cobalt/src/logger/project_context_factory.h"
#include "third_party/cobalt/src/public/testing/fake_cobalt_service.h"

namespace cobalt {

const char kTestDir[] = "/tmp/cobalt_app_test";

bool WriteFile(const std::string& file, const std::string& to_write) {
  return files::WriteFile(std::string(kTestDir) + std::string("/") + file, to_write.c_str(),
                          to_write.length());
}

class CreateCobaltConfigTest : public gtest::TestLoopFixture {
 public:
  CreateCobaltConfigTest()
      : ::gtest::TestLoopFixture(),
        context_provider_(),
        clock_(context_provider_.public_service_directory()),
        network_wrapper_(dispatcher()) {}

 protected:
  void SetUp() override {
    ASSERT_TRUE(files::DeletePath(kTestDir, true));
    ASSERT_TRUE(files::CreateDirectory(kTestDir));
  }

  CobaltConfig CreateCobaltConfig(
      const std::string& metrics_registry_path, const FuchsiaConfigurationData& configuration_data,
      std::chrono::seconds target_interval, std::chrono::seconds min_interval,
      std::chrono::seconds initial_interval, size_t event_aggregator_backfill_days,
      bool use_memory_observation_store, size_t max_bytes_per_observation_store,
      const std::string& product_name, const std::string& board_name, const std::string& version) {
    auto global_project_context_factory = std::make_shared<logger::ProjectContextFactory>(
        ReadGlobalMetricsRegistryBytes(metrics_registry_path));

    return CobaltApp::CreateCobaltConfig(
        dispatcher(), global_project_context_factory.get(), configuration_data, &clock_,
        &network_wrapper_, target_interval, min_interval, initial_interval,
        event_aggregator_backfill_days, use_memory_observation_store,
        max_bytes_per_observation_store, product_name, board_name, version);
  }

  sys::testing::ComponentContextProvider context_provider_;
  FuchsiaSystemClock clock_;
  network_wrapper::FakeNetworkWrapper network_wrapper_;
};

TEST_F(CreateCobaltConfigTest, Devel) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "DEVEL"));
  ASSERT_TRUE(WriteFile("config.json", "{\"release_stage\": \"DEBUG\"}"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir);
  CobaltConfig config = CreateCobaltConfig(
      "/pkg/data/testapp_metrics_registry.pb", configuration_data, std::chrono::seconds(1),
      std::chrono::seconds(2), std::chrono::seconds(3), 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.target_pipeline->environment(), system_data::Environment::DEVEL);
  EXPECT_EQ(config.release_stage, ReleaseStage::DEBUG);
}

TEST_F(CreateCobaltConfigTest, Local) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "LOCAL"));
  ASSERT_TRUE(WriteFile("config.json", "{\"release_stage\": \"DEBUG\"}"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir);
  CobaltConfig config = CreateCobaltConfig(
      "/pkg/data/testapp_metrics_registry.pb", configuration_data, std::chrono::seconds(1),
      std::chrono::seconds(2), std::chrono::seconds(3), 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.target_pipeline->environment(), system_data::Environment::LOCAL);
  EXPECT_EQ(config.release_stage, ReleaseStage::DEBUG);
}

TEST_F(CreateCobaltConfigTest, GA) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "DEVEL"));
  ASSERT_TRUE(WriteFile("config.json", "{\"release_stage\": \"GA\"}"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir);
  CobaltConfig config = CreateCobaltConfig(
      "/pkg/data/testapp_metrics_registry.pb", configuration_data, std::chrono::seconds(1),
      std::chrono::seconds(2), std::chrono::seconds(3), 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.target_pipeline->environment(), system_data::Environment::DEVEL);
  EXPECT_EQ(config.release_stage, ReleaseStage::GA);
}

TEST_F(CreateCobaltConfigTest, InternalLogger) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "DEVEL"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir);
  CobaltConfig config = CreateCobaltConfig(
      // Use the global metrics registry that contains the cobalt_internal project.
      "/pkg/data/global_metrics_registry.pb", configuration_data, std::chrono::seconds(1),
      std::chrono::seconds(2), std::chrono::seconds(3), 4, true, 1048000, "core", "x64", "0.1.2");
  ASSERT_NE(config.internal_logger_project_context, nullptr);
  std::string internal_metrics_name = config.internal_logger_project_context->FullyQualifiedName();
  EXPECT_TRUE(absl::StrContains(internal_metrics_name, "cobalt_internal"));
}

TEST_F(CreateCobaltConfigTest, InternalLoggerNotFound) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "DEVEL"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir);
  CobaltConfig config = CreateCobaltConfig(
      // Use the testapp metrics registry that does not contain the cobalt_internal project.
      "/pkg/data/testapp_metrics_registry.pb", configuration_data, std::chrono::seconds(1),
      std::chrono::seconds(2), std::chrono::seconds(3), 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.internal_logger_project_context, nullptr);
}

class CobaltAppTest : public gtest::TestLoopFixture {
 public:
  CobaltAppTest()
      : ::gtest::TestLoopFixture(),
        context_provider_(),
        testapp_project_context_factory_(std::make_shared<logger::ProjectContextFactory>(
            ReadGlobalMetricsRegistryBytes("/pkg/data/testapp_metrics_registry.pb"))),
        timekeeper_(context_provider_.context()),
        clock_(new FuchsiaSystemClock(context_provider_.public_service_directory())),
        fake_service_(new testing::FakeCobaltService()),
        cobalt_app_(context_provider_.TakeContext(), dispatcher(),
                    std::unique_ptr<testing::FakeCobaltService>(fake_service_),
                    std::unique_ptr<FuchsiaSystemClock>(clock_),
                    std::make_unique<network_wrapper::FakeNetworkWrapper>(dispatcher()),
                    testapp_project_context_factory_, true, false) {}

 protected:
  void SetUp() override {
    context_provider_.ConnectToPublicService(factory_.NewRequest());
    ASSERT_NE(factory_.get(), nullptr);
  }

  // Call the LoggerFactory to create a Logger connection.
  fuchsia::cobalt::LoggerPtr GetLogger() {
    fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
    fuchsia::cobalt::LoggerPtr logger;
    factory_->CreateLoggerFromProjectId(testapp_registry::kProjectId, logger.NewRequest(),
                                        [&](fuchsia::cobalt::Status status_) { status = status_; });
    RunLoopUntilIdle();
    EXPECT_EQ(status, fuchsia::cobalt::Status::OK);
    EXPECT_NE(logger.get(), nullptr);
    return logger;
  }

  sys::testing::ComponentContextProvider context_provider_;
  std::shared_ptr<logger::ProjectContextFactory> testapp_project_context_factory_;
  testapp::FakeTimekeeper timekeeper_;
  FuchsiaSystemClock* clock_;
  testing::FakeCobaltService* fake_service_;
  CobaltApp cobalt_app_;
  fuchsia::cobalt::LoggerFactoryPtr factory_;
};

TEST_F(CobaltAppTest, CreateLogger) {
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  EXPECT_EQ(fake_logger, nullptr);
  fuchsia::cobalt::LoggerPtr logger = GetLogger();
  fake_logger = fake_service_->last_logger_created();
  EXPECT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);
}

TEST_F(CobaltAppTest, SystemClockIsAccurate) {
  // Give the fake_timekeeper/clock time to become accurate.
  RunLoopUntilIdle();
  EXPECT_EQ(fake_service_->system_clock_is_accurate(), true);
}

TEST_F(CobaltAppTest, LogEvent) {
  fuchsia::cobalt::LoggerPtr logger = GetLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  Status status = Status::INTERNAL_ERROR;
  logger->LogEvent(testapp_registry::kErrorOccurredMetricId, 0,
                   [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_logger->call_count(), 1);
}

TEST_F(CobaltAppTest, SetChannel) {
  fuchsia::cobalt::SystemDataUpdaterPtr system_data_updater;
  context_provider_.ConnectToPublicService(system_data_updater.NewRequest());

  Status status = Status::INTERNAL_ERROR;
  system_data_updater->SetChannel("new-channel-name",
                                  [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_service_->system_data()->channel(), "new-channel-name");
}

}  // namespace cobalt
