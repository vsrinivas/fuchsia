// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_app.h"

#include <fuchsia/metrics/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gmock/gmock.h>
#include <sdk/lib/sys/cpp/testing/service_directory_provider.h>

#include "fuchsia/process/lifecycle/cpp/fidl.h"
#include "src/cobalt/bin/app/activity_listener_impl.h"
#include "src/cobalt/bin/app/diagnostics_impl.h"
#include "src/cobalt/bin/app/testapp_metrics_registry.cb.h"
#include "src/cobalt/bin/testing/fake_clock.h"
#include "src/cobalt/bin/testing/fake_http_loader.h"
#include "src/lib/cobalt/cpp/metric_event_builder.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/public/cobalt_config.h"
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
      : ::gtest::TestLoopFixture(), context_provider_(), clock_(dispatcher(), inspect::Node()) {}

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
      std::chrono::seconds initial_interval, float jitter, size_t event_aggregator_backfill_days,
      bool use_memory_observation_store, size_t max_bytes_per_observation_store,
      const std::string& product_name, const std::string& board_name, const std::string& version) {
    return CobaltApp::CreateCobaltConfig(
        dispatcher(), metrics_registry_path, configuration_data, &clock_,
        [this] {
          fuchsia::net::http::LoaderSyncPtr loader_sync;
          service_directory_provider_.service_directory()->Connect(loader_sync.NewRequest());
          return loader_sync;
        },
        UploadScheduleConfig{target_interval, min_interval, initial_interval, jitter},
        event_aggregator_backfill_days, /*test_dont_backfill_empty_reports=*/false,
        use_memory_observation_store, max_bytes_per_observation_store,
        cobalt::kDefaultStorageQuotas, product_name, board_name, version,
        std::make_unique<ActivityListenerImpl>(dispatcher(), context_provider_.context()->svc()),
        std::make_unique<DiagnosticsImpl>(inspect::Node()));
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
  CobaltConfig config =
      CreateCobaltConfig("/pkg/data/testapp_metrics_registry.pb", configuration_data,
                         std::chrono::seconds(1), std::chrono::seconds(2), std::chrono::seconds(3),
                         0, 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.target_pipeline->environment(), system_data::Environment::DEVEL);
  EXPECT_EQ(config.release_stage, ReleaseStage::DEBUG);
}

TEST_F(CreateCobaltConfigTest, Local) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "LOCAL"));
  ASSERT_TRUE(WriteFile("config.json", "{\"release_stage\": \"DEBUG\"}"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir);
  CobaltConfig config =
      CreateCobaltConfig("/pkg/data/testapp_metrics_registry.pb", configuration_data,
                         std::chrono::seconds(1), std::chrono::seconds(2), std::chrono::seconds(3),
                         0, 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.target_pipeline->environment(), system_data::Environment::LOCAL);
  EXPECT_EQ(config.release_stage, ReleaseStage::DEBUG);
}

TEST_F(CreateCobaltConfigTest, GA) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "DEVEL"));
  ASSERT_TRUE(WriteFile("config.json", "{\"release_stage\": \"GA\"}"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir);
  CobaltConfig config =
      CreateCobaltConfig("/pkg/data/testapp_metrics_registry.pb", configuration_data,
                         std::chrono::seconds(1), std::chrono::seconds(2), std::chrono::seconds(3),
                         0, 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.target_pipeline->environment(), system_data::Environment::DEVEL);
  EXPECT_EQ(config.release_stage, ReleaseStage::GA);
}

TEST_F(CreateCobaltConfigTest, ConfigFields) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "DEVEL"));
  ASSERT_TRUE(WriteFile("config.json", "{\"release_stage\": \"GA\"}"));
  float test_jitter = .3;
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir);
  CobaltConfig config =
      CreateCobaltConfig("/pkg/data/testapp_metrics_registry.pb", configuration_data,
                         std::chrono::seconds(1), std::chrono::seconds(2), std::chrono::seconds(3),
                         test_jitter, 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.upload_schedule_cfg.jitter, test_jitter);
}

TEST_F(CreateCobaltConfigTest, BuildTypeUser) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "DEVEL"));
  ASSERT_TRUE(WriteFile("type", "user\n"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir, kTestDir);
  CobaltConfig config =
      CreateCobaltConfig("/pkg/data/testapp_metrics_registry.pb", configuration_data,
                         std::chrono::seconds(1), std::chrono::seconds(2), std::chrono::seconds(3),
                         0, 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.build_type, SystemProfile::USER);
}

TEST_F(CreateCobaltConfigTest, UnknownBuildType) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "DEVEL"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir, kTestDir);
  CobaltConfig config =
      CreateCobaltConfig("/pkg/data/testapp_metrics_registry.pb", configuration_data,
                         std::chrono::seconds(1), std::chrono::seconds(2), std::chrono::seconds(3),
                         0, 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.build_type, SystemProfile::UNKNOWN_TYPE);
}

TEST_F(CreateCobaltConfigTest, InvalidBuildType) {
  ASSERT_TRUE(WriteFile("cobalt_environment", "DEVEL"));
  ASSERT_TRUE(WriteFile("type", "invalid"));
  FuchsiaConfigurationData configuration_data(kTestDir, kTestDir, kTestDir);
  CobaltConfig config =
      CreateCobaltConfig("/pkg/data/testapp_metrics_registry.pb", configuration_data,
                         std::chrono::seconds(1), std::chrono::seconds(2), std::chrono::seconds(3),
                         0, 4, true, 1048000, "core", "x64", "0.1.2");
  EXPECT_EQ(config.build_type, SystemProfile::OTHER_TYPE);
}

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using ::testing::UnorderedElementsAre;

class CobaltAppTest : public gtest::TestLoopFixture {
 public:
  CobaltAppTest()
      : ::gtest::TestLoopFixture(),
        context_provider_(),
        clock_(new FakeFuchsiaSystemClock(dispatcher())),
        fake_service_(new testing::FakeCobaltService()),
        cobalt_app_(
            context_provider_.TakeContext(), dispatcher(), lifecycle_.NewRequest(dispatcher()),
            []() { /* Stub shutdown callback */ }, inspector_.GetRoot().CreateChild("cobalt_app"),
            inspect::Node(), std::unique_ptr<testing::FakeCobaltService>(fake_service_),
            std::unique_ptr<FakeFuchsiaSystemClock>(clock_), true,
            /*test_dont_backfill_empty_reports=*/false, false) {}

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
  //
  // If experiments are provided, uses CreateMetricEventLoggerWithExperiments
  // instead.
  fuchsia::metrics::MetricEventLoggerPtr GetMetricEventLogger(
      int project_id = testapp_registry::kProjectId, std::vector<uint32_t> experiment_ids = {}) {
    fpromise::result<void, fuchsia::metrics::Error> result;
    fuchsia::metrics::ProjectSpec project;
    project.set_customer_id(1);
    project.set_project_id(project_id);
    fuchsia::metrics::MetricEventLoggerPtr logger;
    if (experiment_ids.empty()) {
      metric_event_logger_factory_->CreateMetricEventLogger(
          std::move(project), logger.NewRequest(),
          [&](auto result_) { result = std::move(result_); });
    } else {
      metric_event_logger_factory_->CreateMetricEventLoggerWithExperiments(
          std::move(project), std::move(experiment_ids), logger.NewRequest(),
          [&](auto result_) { result = std::move(result_); });
    }
    RunLoopUntilIdle();
    EXPECT_TRUE(result.is_ok());
    EXPECT_NE(logger.get(), nullptr);
    return logger;
  }

  fuchsia::cobalt::Controller* GetCobaltController() { return cobalt_app_.controller_impl_.get(); }

  fuchsia::process::lifecycle::LifecyclePtr lifecycle_;
  sys::testing::ComponentContextProvider context_provider_;
  FakeFuchsiaSystemClock* clock_;
  testing::FakeCobaltService* fake_service_;
  inspect::Inspector inspector_;
  CobaltApp cobalt_app_;
  fuchsia::cobalt::LoggerFactoryPtr factory_;
  fuchsia::metrics::MetricEventLoggerFactoryPtr metric_event_logger_factory_;
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

  // Give the clock time to become accurate.
  RunLoopUntilIdle();
  EXPECT_EQ(fake_service_->system_clock_is_accurate(), true);
  EXPECT_TRUE(callback_invoked);
}

TEST_F(CobaltAppTest, InspectData) {
  fpromise::result<inspect::Hierarchy> result = inspect::ReadFromVmo(inspector_.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  EXPECT_THAT(
      result.take_value(),
      AllOf(NodeMatches(NameMatches("root")),
            ChildrenMatch(UnorderedElementsAre(AllOf(
                NodeMatches(NameMatches("cobalt_app")),
                ChildrenMatch(UnorderedElementsAre(NodeMatches(NameMatches("system_data")))))))));
}

TEST_F(CobaltAppTest, LogEvent) {
  fuchsia::cobalt::LoggerPtr logger = GetLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
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

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
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
  fuchsia::metrics::MetricEventLoggerPtr logger = GetMetricEventLogger();
  fake_logger = fake_service_->last_logger_created();
  EXPECT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);
}

TEST_F(CobaltAppTest, CreateMetricEventLoggerWithExperiments) {
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  EXPECT_EQ(fake_logger, nullptr);
  std::vector<uint32_t> test_experiments = {123456789, 987654321};
  fuchsia::metrics::MetricEventLoggerPtr logger =
      GetMetricEventLogger(testapp_registry::kProjectId, test_experiments);
  fake_logger = fake_service_->last_logger_created();
  EXPECT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);
}

TEST_F(CobaltAppTest, CreateMetricEventLoggerNoValidLogger) {
  // Make sure that the CobaltService returns nullptr for the next call to NewLogger().
  fake_service_->FailNextNewLogger();

  fpromise::result<void, fuchsia::metrics::Error> result;
  fuchsia::metrics::ProjectSpec project;
  project.set_customer_id(1);
  project.set_project_id(987654321);
  fuchsia::metrics::MetricEventLoggerPtr logger;
  metric_event_logger_factory_->CreateMetricEventLogger(
      std::move(project), logger.NewRequest(), [&](auto result_) { result = std::move(result_); });
  RunLoopUntilIdle();
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), fuchsia::metrics::Error::INVALID_ARGUMENTS);
}

TEST_F(CobaltAppTest, LogOccurrence) {
  fuchsia::metrics::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  fpromise::result<void, fuchsia::metrics::Error> result;
  logger->LogOccurrence(testapp_registry::kErrorOccurredNewMetricId, /*count=*/1,
                        /*event_codes=*/{2}, [&](auto result_) { result = std::move(result_); });
  RunLoopUntilIdle();
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kErrorOccurredNewMetricId);
  EXPECT_EQ(event.occurrence_event().count(), 1);
  EXPECT_THAT(event.occurrence_event().event_code(), ::testing::ElementsAre(2));
}

TEST_F(CobaltAppTest, LogInteger) {
  fuchsia::metrics::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  fpromise::result<void, fuchsia::metrics::Error> result;
  logger->LogInteger(testapp_registry::kUpdateDurationNewMetricId, /*value=*/-42,
                     /*event_codes=*/{3}, [&](auto result_) { result = std::move(result_); });
  RunLoopUntilIdle();
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kUpdateDurationNewMetricId);
  EXPECT_EQ(event.integer_event().value(), -42);
  EXPECT_THAT(event.integer_event().event_code(), ::testing::ElementsAre(3));
}

TEST_F(CobaltAppTest, LogIntegerHistogram) {
  fuchsia::metrics::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  fpromise::result<void, fuchsia::metrics::Error> result;
  std::vector<fuchsia::metrics::HistogramBucket> histogram;
  fuchsia::metrics::HistogramBucket entry;
  entry.index = 1;
  entry.count = 42;
  histogram.push_back(entry);
  logger->LogIntegerHistogram(testapp_registry::kBandwidthUsageNewMetricId, std::move(histogram),
                              /*event_codes=*/{4, 5},
                              [&](auto result_) { result = std::move(result_); });
  RunLoopUntilIdle();
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kBandwidthUsageNewMetricId);
  EXPECT_EQ(event.integer_histogram_event().buckets().size(), 1);
  EXPECT_EQ(event.integer_histogram_event().buckets(0).index(), 1);
  EXPECT_EQ(event.integer_histogram_event().buckets(0).count(), 42);
  EXPECT_THAT(event.integer_histogram_event().event_code(), ::testing::ElementsAre(4, 5));
}

TEST_F(CobaltAppTest, LogString) {
  fuchsia::metrics::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  fpromise::result<void, fuchsia::metrics::Error> result;
  logger->LogString(testapp_registry::kErrorOccurredComponentsMetricId,
                    /*string_value=*/"component", /*event_codes=*/{3},
                    [&](auto result_) { result = std::move(result_); });
  RunLoopUntilIdle();
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(fake_logger->call_count(), 1);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kErrorOccurredComponentsMetricId);
  EXPECT_EQ(event.string_event().string_value(), "component");
  EXPECT_THAT(event.string_event().event_code(), ::testing::ElementsAre(3));
}

TEST_F(CobaltAppTest, LogMetricEvents) {
  FX_LOGS(INFO) << "A logging statement";
  fuchsia::metrics::MetricEventLoggerPtr logger = GetMetricEventLogger();
  logger::testing::FakeLogger* fake_logger = fake_service_->last_logger_created();
  ASSERT_NE(fake_logger, nullptr);
  EXPECT_EQ(fake_logger->call_count(), 0);

  fpromise::result<void, fuchsia::metrics::Error> result;
  std::vector<fuchsia::metrics::MetricEvent> events;
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
  logger->LogMetricEvents(std::move(events), [&](auto result_) { result = std::move(result_); });
  RunLoopUntilIdle();
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(fake_logger->call_count(), 5);
  auto event = fake_logger->last_event_logged();
  EXPECT_EQ(event.metric_id(), testapp_registry::kErrorOccurredNewMetricId);
  EXPECT_EQ(event.occurrence_event().count(), 5);
  EXPECT_THAT(event.occurrence_event().event_code(), ::testing::ElementsAre(2));
}

TEST_F(CobaltAppTest, ShutDown) {
  EXPECT_EQ(fake_service_->is_shut_down(), false);

  fuchsia::metrics::MetricEventLoggerPtr metric_logger = GetMetricEventLogger();
  fuchsia::cobalt::LoggerPtr logger = GetLogger();
  EXPECT_TRUE(metric_logger.is_bound());
  EXPECT_TRUE(logger.is_bound());

  lifecycle_->Stop();
  RunLoopUntilIdle();
  EXPECT_EQ(fake_service_->is_shut_down(), true);
  EXPECT_FALSE(metric_logger.is_bound());
  EXPECT_FALSE(logger.is_bound());
  EXPECT_FALSE(lifecycle_.is_bound());
}

TEST_F(CobaltAppTest, NoNewLoggersAfterShutDown) {
  lifecycle_->Stop();
  RunLoopUntilIdle();

  fuchsia::metrics::MetricEventLoggerPtr metric_logger = nullptr;
  fuchsia::cobalt::LoggerPtr logger = nullptr;
  fuchsia::cobalt::Status cobalt_status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  fuchsia::metrics::MetricEventLoggerFactory_CreateMetricEventLogger_Result metrics_result;
  fuchsia::metrics::ProjectSpec project;
  project.set_customer_id(1);
  project.set_project_id(testapp_registry::kProjectId);

  metric_event_logger_factory_->CreateMetricEventLogger(
      std::move(project), metric_logger.NewRequest(),
      [&](fuchsia::metrics::MetricEventLoggerFactory_CreateMetricEventLogger_Result result) {
        metrics_result = std::move(result);
      });
  RunLoopUntilIdle();
  EXPECT_TRUE(metrics_result.is_err());
  EXPECT_EQ(metrics_result.err(), fuchsia::metrics::Error::SHUT_DOWN);
  EXPECT_FALSE(metric_logger.is_bound());

  factory_->CreateLoggerFromProjectId(
      testapp_registry::kProjectId, logger.NewRequest(),
      [&](fuchsia::cobalt::Status status_) { cobalt_status = status_; });
  RunLoopUntilIdle();
  EXPECT_EQ(cobalt_status, fuchsia::cobalt::Status::SHUT_DOWN);
  EXPECT_FALSE(logger.is_bound());
}

}  // namespace cobalt
