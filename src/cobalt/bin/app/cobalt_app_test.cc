// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_app.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gmock/gmock.h>

#include "sdk/lib/sys/cpp/testing/service_directory_provider.h"
#include "src/cobalt/bin/app/activity_listener_impl.h"
#include "src/cobalt/bin/app/testapp_metrics_registry.cb.h"
#include "src/cobalt/bin/testapp/fake_timekeeper.h"
#include "src/cobalt/bin/testing/fake_http_loader.h"
#include "src/lib/cobalt/cpp/metric_event_builder.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "third_party/abseil-cpp/absl/strings/match.h"
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
        clock_(context_provider_.public_service_directory()) {}

 protected:
  void SetUp() override {
    ASSERT_TRUE(files::DeletePath(kTestDir, true));
    ASSERT_TRUE(files::CreateDirectory(kTestDir));

    loader_ = std::make_unique<FakeHTTPLoader>(dispatcher());
    service_directory_provider_.AddService(loader_->GetHandler());
  }

  CobaltConfig CreateCobaltConfig(
      const std::string& metrics_registry_path, const FuchsiaConfigurationData& configuration_data,
      std::chrono::seconds target_interval, std::chrono::seconds min_interval,
      std::chrono::seconds initial_interval, size_t event_aggregator_backfill_days,
      bool use_memory_observation_store, size_t max_bytes_per_observation_store,
      const std::string& product_name, const std::string& board_name, const std::string& version) {
    return CobaltApp::CreateCobaltConfig(
        dispatcher(), metrics_registry_path, configuration_data, &clock_,
        [this] {
          fuchsia::net::http::LoaderSyncPtr loader_sync;
          service_directory_provider_.service_directory()->Connect(loader_sync.NewRequest());
          return loader_sync;
        },
        target_interval, min_interval, initial_interval, event_aggregator_backfill_days,
        use_memory_observation_store, max_bytes_per_observation_store, product_name, board_name,
        version,
        std::make_unique<ActivityListenerImpl>(dispatcher(), context_provider_.context()->svc()));
  }

  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::unique_ptr<FakeHTTPLoader> loader_;

  sys::testing::ComponentContextProvider context_provider_;
  FuchsiaSystemClock clock_;
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

class CobaltAppTest : public gtest::TestLoopFixture {
 public:
  CobaltAppTest()
      : ::gtest::TestLoopFixture(),
        context_provider_(),
        timekeeper_(context_provider_.context()),
        clock_(new FuchsiaSystemClock(context_provider_.public_service_directory())),
        fake_service_(new testing::FakeCobaltService()),
        cobalt_app_(context_provider_.TakeContext(), dispatcher(),
                    std::unique_ptr<testing::FakeCobaltService>(fake_service_),
                    std::unique_ptr<FuchsiaSystemClock>(clock_), true, false) {}

 protected:
  void SetUp() override {
    context_provider_.ConnectToPublicService(factory_.NewRequest());
    ASSERT_NE(factory_.get(), nullptr);
    context_provider_.ConnectToPublicService(metric_event_logger_factory_.NewRequest());
    ASSERT_NE(metric_event_logger_factory_.get(), nullptr);
  }

  // Call the LoggerFactory to create a Logger connection.
  fuchsia::cobalt::LoggerPtr GetLogger(int project_id = testapp_registry::kProjectId) {
    fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
    fuchsia::cobalt::LoggerPtr logger;
    factory_->CreateLoggerFromProjectId(project_id, logger.NewRequest(),
                                        [&](fuchsia::cobalt::Status status_) { status = status_; });
    RunLoopUntilIdle();
    EXPECT_EQ(status, fuchsia::cobalt::Status::OK);
    EXPECT_NE(logger.get(), nullptr);
    return logger;
  }

  // Call the MetricEventLoggerFactory to create a Logger connection.
  fuchsia::cobalt::MetricEventLoggerPtr GetMetricEventLogger(
      int project_id = testapp_registry::kProjectId) {
    fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
    fuchsia::cobalt::ProjectSpec project;
    project.set_customer_id(1);
    project.set_project_id(project_id);
    fuchsia::cobalt::MetricEventLoggerPtr logger;
    metric_event_logger_factory_->CreateMetricEventLogger(
        std::move(project), logger.NewRequest(),
        [&](fuchsia::cobalt::Status status_) { status = status_; });
    RunLoopUntilIdle();
    EXPECT_EQ(status, fuchsia::cobalt::Status::OK);
    EXPECT_NE(logger.get(), nullptr);
    return logger;
  }

  fuchsia::cobalt::Controller* GetCobaltController() { return cobalt_app_.controller_impl_.get(); }

  sys::testing::ComponentContextProvider context_provider_;
  testapp::FakeTimekeeper timekeeper_;
  FuchsiaSystemClock* clock_;
  testing::FakeCobaltService* fake_service_;
  CobaltApp cobalt_app_;
  fuchsia::cobalt::LoggerFactoryPtr factory_;
  fuchsia::cobalt::MetricEventLoggerFactoryPtr metric_event_logger_factory_;
};

TEST_F(CobaltAppTest, CreateLogger) {
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  EXPECT_EQ(fake_logger, nullptr);
  fuchsia::cobalt::LoggerPtr logger = GetLogger();
  fake_logger = fake_service_->last_logger_created();
  EXPECT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);
}

TEST_F(CobaltAppTest, CreateLoggerNoValidLogger) {
  // Make sure that the CobaltService returns nullptr for the next call to NewLogger().
  fake_service_->FailNextNewLogger();

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::OK;
  fuchsia::cobalt::LoggerPtr logger;
  factory_->CreateLoggerFromProjectId(/*project_id=*/987654321, logger.NewRequest(),
                                      [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, fuchsia::cobalt::Status::INVALID_ARGUMENTS);
}

TEST_F(CobaltAppTest, CreateLoggerForNonFuchsiaCustomer) {
  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  fuchsia::cobalt::LoggerPtr logger;
  // Use the customer and project ID for the cobalt_internal metrics project.
  factory_->CreateLoggerFromProjectSpec(/*customer_id=*/2147483647, /*project_id=*/205836624,
                                        logger.NewRequest(),
                                        [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_NE(logger.get(), nullptr);
}

TEST_F(CobaltAppTest, SystemClockIsAccurate) {
  bool callback_invoked = false;
  GetCobaltController()->ListenForInitialized([&callback_invoked]() { callback_invoked = true; });
  EXPECT_FALSE(callback_invoked);

  // Give the fake_timekeeper/clock time to become accurate.
  RunLoopUntilIdle();
  EXPECT_EQ(fake_service_->system_clock_is_accurate(), true);
  EXPECT_TRUE(callback_invoked);
}

TEST_F(CobaltAppTest, LogEvent) {
  fuchsia::cobalt::LoggerPtr logger = GetLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  Status status = Status::INTERNAL_ERROR;
  logger->LogEvent(testapp_registry::kErrorOccurredMetricId, /*event_code=*/2,
                   [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kErrorOccurredMetricId);
  EXPECT_EQ(event.event_occurred_event().event_code(), 2);
}

TEST_F(CobaltAppTest, SetSoftwareDistributionInfo) {
  fuchsia::cobalt::SystemDataUpdaterPtr system_data_updater;
  context_provider_.ConnectToPublicService(system_data_updater.NewRequest());

  Status status = Status::INTERNAL_ERROR;
  fuchsia::cobalt::SoftwareDistributionInfo info;
  info.set_current_channel("new-channel-name");
  info.set_current_realm("new-realm-name");
  system_data_updater->SetSoftwareDistributionInfo(
      std::move(info), [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_service_->system_data()->channel(), "new-channel-name");
  EXPECT_EQ(fake_service_->system_data()->realm(), "new-realm-name");
}

TEST_F(CobaltAppTest, CreateMetricEventLogger) {
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  EXPECT_EQ(fake_logger, nullptr);
  fuchsia::cobalt::MetricEventLoggerPtr logger = GetMetricEventLogger();
  fake_logger = fake_service_->last_logger_created();
  EXPECT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);
}

TEST_F(CobaltAppTest, CreateMetricEventLoggerNoValidLogger) {
  // Make sure that the CobaltService returns nullptr for the next call to NewLogger().
  fake_service_->FailNextNewLogger();

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::OK;
  fuchsia::cobalt::ProjectSpec project;
  project.set_customer_id(1);
  project.set_project_id(987654321);
  fuchsia::cobalt::MetricEventLoggerPtr logger;
  metric_event_logger_factory_->CreateMetricEventLogger(
      std::move(project), logger.NewRequest(),
      [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  EXPECT_EQ(status, fuchsia::cobalt::Status::INVALID_ARGUMENTS);
}

TEST_F(CobaltAppTest, LogOccurrence) {
  fuchsia::cobalt::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  Status status = Status::INTERNAL_ERROR;
  logger->LogOccurrence(testapp_registry::kErrorOccurredNewMetricId, /*count=*/1,
                        /*event_codes=*/{2},
                        [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kErrorOccurredNewMetricId);
  EXPECT_EQ(event.occurrence_event().count(), 1);
  EXPECT_THAT(event.occurrence_event().event_code(), ::testing::ElementsAre(2));
}

TEST_F(CobaltAppTest, LogInteger) {
  fuchsia::cobalt::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  Status status = Status::INTERNAL_ERROR;
  logger->LogInteger(testapp_registry::kUpdateDurationNewMetricId, /*value=*/-42,
                     /*event_codes=*/{3},
                     [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kUpdateDurationNewMetricId);
  EXPECT_EQ(event.integer_event().value(), -42);
  EXPECT_THAT(event.integer_event().event_code(), ::testing::ElementsAre(3));
}

TEST_F(CobaltAppTest, LogIntegerHistogram) {
  fuchsia::cobalt::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  Status status = Status::INTERNAL_ERROR;
  std::vector<fuchsia::cobalt::HistogramBucket> histogram;
  fuchsia::cobalt::HistogramBucket entry;
  entry.index = 1;
  entry.count = 42;
  histogram.push_back(entry);
  logger->LogIntegerHistogram(testapp_registry::kBandwidthUsageNewMetricId, std::move(histogram),
                              /*event_codes=*/{4, 5},
                              [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kBandwidthUsageNewMetricId);
  EXPECT_EQ(event.integer_histogram_event().buckets().size(), 1);
  EXPECT_EQ(event.integer_histogram_event().buckets(0).index(), 1);
  EXPECT_EQ(event.integer_histogram_event().buckets(0).count(), 42);
  EXPECT_THAT(event.integer_histogram_event().event_code(), ::testing::ElementsAre(4, 5));
}

TEST_F(CobaltAppTest, LogString) {
  fuchsia::cobalt::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  Status status = Status::INTERNAL_ERROR;
  logger->LogString(testapp_registry::kErrorOccurredComponentsMetricId,
                    /*string_value=*/"component", /*event_codes=*/{3},
                    [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kErrorOccurredComponentsMetricId);
  EXPECT_EQ(event.string_event().string_value(), "component");
  EXPECT_THAT(event.string_event().event_code(), ::testing::ElementsAre(3));
}

TEST_F(CobaltAppTest, LogMetricEvents) {
  FX_LOGS(INFO) << "A logging statement";
  fuchsia::cobalt::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  Status status = Status::INTERNAL_ERROR;
  std::vector<fuchsia::cobalt::MetricEvent> events;
  events.push_back(MetricEventBuilder(testapp_registry::kErrorOccurredNewMetricId)
                       .with_event_code(2)
                       .as_occurrence(1));
  events.push_back(MetricEventBuilder(testapp_registry::kErrorOccurredNewMetricId)
                       .with_event_code(2)
                       .as_occurrence(2));
  events.push_back(MetricEventBuilder(testapp_registry::kErrorOccurredNewMetricId)
                       .with_event_code(2)
                       .as_occurrence(3));
  events.push_back(MetricEventBuilder(testapp_registry::kErrorOccurredNewMetricId)
                       .with_event_code(2)
                       .as_occurrence(4));
  events.push_back(MetricEventBuilder(testapp_registry::kErrorOccurredNewMetricId)
                       .with_event_code(2)
                       .as_occurrence(5));
  logger->LogMetricEvents(std::move(events),
                          [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_logger->call_count(), 5);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kErrorOccurredNewMetricId);
  EXPECT_EQ(event.occurrence_event().count(), 5);
  EXPECT_THAT(event.occurrence_event().event_code(), ::testing::ElementsAre(2));
}

TEST_F(CobaltAppTest, LogCustomEvent) {
  FX_LOGS(INFO) << "A logging statement";
  fuchsia::cobalt::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  Status status = Status::INTERNAL_ERROR;
  std::vector<fuchsia::cobalt::CustomEventValue> parts(3);
  parts.at(0).dimension_name = "query";
  parts.at(0).value.set_string_value("SELECT 1;");
  parts.at(1).dimension_name = "wait_time_ms";
  parts.at(1).value.set_int_value(1234);
  parts.at(2).dimension_name = "response_code";
  parts.at(2).value.set_index_value(2);
  logger->LogCustomEvent(testapp_registry::kQueryResponseMetricId, std::move(parts),
                         [&](fuchsia::cobalt::Status status_) { status = status_; });
  RunLoopUntilIdle();
  ASSERT_EQ(status, fuchsia::cobalt::Status::OK);
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kQueryResponseMetricId);
  EXPECT_EQ(event.custom_event().values().size(), 3);
  EXPECT_EQ(event.custom_event().values().at("query").string_value(), "SELECT 1;");
  EXPECT_EQ(event.custom_event().values().at("wait_time_ms").int_value(), 1234);
  EXPECT_EQ(event.custom_event().values().at("response_code").index_value(), 2);
}

TEST_F(CobaltAppTest, ShutDown) {
  EXPECT_EQ(fake_service_->is_shut_down(), false);
  fuchsia::process::lifecycle::LifecyclePtr process_lifecycle;
  context_provider_.ConnectToPublicService(process_lifecycle.NewRequest(),
                                           "fuchsia.process.lifecycle.Lifecycle");
  process_lifecycle->Stop();
  RunLoopUntilIdle();
  EXPECT_EQ(fake_service_->is_shut_down(), true);
}

}  // namespace cobalt
