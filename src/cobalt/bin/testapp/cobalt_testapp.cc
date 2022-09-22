// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This application is intenteded to be used for manual testing of
// the Cobalt logger client on Fuchsia by Cobalt engineers.
//
// It also serves as an example of how to use the Cobalt FIDL API.
//
// It is also invoked by the cobalt_client CQ and CI.

#include "src/cobalt/bin/testapp/cobalt_testapp.h"

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "fuchsia/component/cpp/fidl.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/sys/component/cpp/testing/scoped_child.h"
#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"
#include "src/cobalt/bin/testapp/prober_metrics_registry.cb.h"
#include "src/cobalt/bin/testapp/testapp_metrics_registry.cb.h"
#include "src/cobalt/bin/testapp/tests.h"
#include "src/cobalt/bin/utils/status_utils.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/macros.h"

namespace cobalt::testapp {

using ::cobalt::StatusToString;

constexpr char kCobaltWithEventAggregatorWorker[] = "#meta/cobalt_with_event_aggregator_worker.cm";
constexpr char kCobaltNoEventAggregatorWorker[] = "#meta/cobalt_no_event_aggregator_worker.cm";

constexpr uint32_t kControlId = 48954961;
constexpr uint32_t kExperimentId = 48954962;

#define TRY_TEST(test) \
  if (!(test)) {       \
    return false;      \
  }

#define CONNECT_AND_TRY_TEST_TWICE(test, variant)                                 \
  {                                                                               \
    std::unique_ptr<component_testing::ScopedChild> child =                       \
        std::make_unique<component_testing::ScopedChild>(Connect(variant));       \
    if (!(test)) {                                                                \
      child->MakeTeardownAsync(loop_->dispatcher());                              \
      child = std::make_unique<component_testing::ScopedChild>(Connect(variant)); \
      if (!(test)) {                                                              \
        return false;                                                             \
      }                                                                           \
    }                                                                             \
    child->MakeTeardownAsync(loop_->dispatcher());                                \
  }

bool CobaltTestApp::RunTests() {
  std::unique_ptr<component_testing::ScopedChild> child =
      std::make_unique<component_testing::ScopedChild>(Connect(kCobaltWithEventAggregatorWorker));

  // TODO(zmbush): Create tests for all logger methods.
  TRY_TEST(TestLogEvent(&logger_));
  TRY_TEST(TestLogEventCount(&logger_));
  TRY_TEST(TestLogElapsedTime(&logger_));
  TRY_TEST(TestLogFrameRate(&logger_));
  TRY_TEST(TestLogMemoryUsage(&logger_));
  TRY_TEST(TestLogIntHistogram(&logger_));
  TRY_TEST(TestLogCustomEvent(&logger_));
  TRY_TEST(TestLogCobaltEvent(&logger_));
  child->MakeTeardownAsync(loop_->dispatcher());

  return DoLocalAggregationTests(kEventAggregatorBackfillDays, kCobaltNoEventAggregatorWorker);
}

bool CobaltTestApp::DoLocalAggregationTests(const size_t backfill_days,
                                            const std::string &variant) {
  uint32_t project_id =
      (test_for_prober_ ? cobalt_prober_registry::kProjectId : cobalt_registry::kProjectId);
  // TODO(fxbug.dev/52750): We try each of these tests twice in case the failure
  // reason is that the calendar date has changed mid-test.
  CONNECT_AND_TRY_TEST_TWICE(
      TestLogEventWithAggregation(&logger_, clock_.get(), &cobalt_controller_, backfill_days,
                                  project_id),
      variant);
  CONNECT_AND_TRY_TEST_TWICE(
      TestLogEventCountWithAggregation(&logger_, clock_.get(), &cobalt_controller_, backfill_days,
                                       project_id),
      variant);
  CONNECT_AND_TRY_TEST_TWICE(
      TestLogElapsedTimeWithAggregation(&logger_, clock_.get(), &cobalt_controller_, backfill_days,
                                        project_id),
      variant);
  CONNECT_AND_TRY_TEST_TWICE(
      TestLogInteger(&logger_, clock_.get(), &cobalt_controller_, backfill_days, project_id),
      variant);
  CONNECT_AND_TRY_TEST_TWICE(
      TestLogOccurrence(&logger_, clock_.get(), &cobalt_controller_, backfill_days, project_id),
      variant);
  CONNECT_AND_TRY_TEST_TWICE(TestLogIntegerHistogram(&logger_, clock_.get(), &cobalt_controller_,
                                                     backfill_days, project_id),
                             variant);
  CONNECT_AND_TRY_TEST_TWICE(
      TestLogString(&logger_, clock_.get(), &cobalt_controller_, backfill_days, project_id),
      variant);

  return true;
}

component_testing::ScopedChild CobaltTestApp::Connect(const std::string &variant) {
  fuchsia::component::RealmSyncPtr realm_proxy;
  FX_CHECK(ZX_OK == context_->svc()->Connect(realm_proxy.NewRequest()))
      << "Failed to connect to fuchsia.component.Realm";

  auto child = component_testing::ScopedChild::New(
      std::move(realm_proxy), "realm_builder",
      "cobalt_under_test_" + std::to_string(scoped_children_), variant);
  logger_.SetCobaltUnderTestMoniker("realm_builder\\:" + child.GetChildName());
  scoped_children_ += 1;

  fuchsia::cobalt::LoggerFactorySyncPtr logger_factory =
      child.ConnectSync<fuchsia::cobalt::LoggerFactory>();
  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  uint32_t project_id =
      (test_for_prober_ ? cobalt_prober_registry::kProjectId : cobalt_registry::kProjectId);
  FX_LOGS(INFO) << "Test app is logging for the " << project_id << " project";
  logger_factory->CreateLoggerFromProjectId(project_id, logger_.logger_.NewRequest(), &status);
  FX_CHECK(status == fuchsia::cobalt::Status::OK) << "CreateLogger() => " << StatusToString(status);

  fuchsia::metrics::MetricEventLoggerFactorySyncPtr metric_event_logger_factory =
      child.ConnectSync<fuchsia::metrics::MetricEventLoggerFactory>();

  fuchsia::metrics::MetricEventLoggerFactory_CreateMetricEventLogger_Result result;
  fuchsia::metrics::ProjectSpec project;
  project.set_customer_id(1);
  project.set_project_id(project_id);
  zx_status_t fx_status = metric_event_logger_factory->CreateMetricEventLogger(
      std::move(project), logger_.metric_event_logger_.NewRequest(), &result);
  FX_CHECK(fx_status == ZX_OK) << "FIDL: CreateMetricEventLogger() => " << fx_status;
  FX_CHECK(!result.is_err()) << "CreateMetricEventLogger() => " << ErrorToString(result.err());

  {
    fuchsia::metrics::MetricEventLoggerFactory_CreateMetricEventLoggerWithExperiments_Result
        result_with_experiments;
    fx_status = metric_event_logger_factory->CreateMetricEventLoggerWithExperiments(
        std::move(project), {kControlId}, logger_.control_metric_event_logger_.NewRequest(),
        &result_with_experiments);
    FX_CHECK(fx_status == ZX_OK) << "FIDL: CreateMetricEventLogger() => " << fx_status;
    FX_CHECK(!result_with_experiments.is_err()) << "CreateMetricEventLoggerWithExperiments() => "
                                                << ErrorToString(result_with_experiments.err());
  }
  {
    fuchsia::metrics::MetricEventLoggerFactory_CreateMetricEventLoggerWithExperiments_Result
        result_with_experiments;
    fx_status = metric_event_logger_factory->CreateMetricEventLoggerWithExperiments(
        std::move(project), {kExperimentId}, logger_.experimental_metric_event_logger_.NewRequest(),
        &result_with_experiments);
    FX_CHECK(fx_status == ZX_OK) << "FIDL: CreateMetricEventLogger() => " << fx_status;
    FX_CHECK(!result_with_experiments.is_err()) << "CreateMetricEventLoggerWithExperiments() => "
                                                << ErrorToString(result_with_experiments.err());
  }

  child.Connect(system_data_updater_.NewRequest());
  status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  fuchsia::cobalt::SoftwareDistributionInfo info;
  info.set_current_channel("devhost");
  info.set_current_realm("");
  system_data_updater_->SetSoftwareDistributionInfo(std::move(info), &status);
  FX_CHECK(status == fuchsia::cobalt::Status::OK) << "Unable to set software distribution info";

  cobalt_controller_ = child.ConnectSync<fuchsia::cobalt::Controller>();

  // Block until the Cobalt service has been fully initialized. This includes
  // being notified by the timekeeper service that the system clock is accurate.
  FX_LOGS(INFO) << "Blocking until the Cobalt service is fully initialized.";
  cobalt_controller_->ListenForInitialized();
  FX_LOGS(INFO) << "Continuing because the Cobalt service is fully initialzied.";
  return child;
}

}  // namespace cobalt::testapp
