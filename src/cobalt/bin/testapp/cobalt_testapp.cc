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
#include <lib/sys/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "fuchsia/sys2/cpp/fidl.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
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

#define TRY_TEST(test) \
  if (!(test)) {       \
    return false;      \
  }

#define CONNECT_AND_TRY_TEST_TWICE(test, variant)       \
  {                                                     \
    sys::testing::ScopedChild child = Connect(variant); \
    if (!(test)) {                                      \
      DropChild(std::move(child));                      \
      child = Connect(variant);                         \
      if (!(test)) {                                    \
        return false;                                   \
      }                                                 \
    }                                                   \
    DropChild(std::move(child));                        \
  }

bool CobaltTestApp::RunTests() {
  sys::testing::ScopedChild child = Connect(kCobaltWithEventAggregatorWorker);

  // TODO(zmbush): Create tests for all logger methods.
  TRY_TEST(TestLogEvent(&logger_));
  TRY_TEST(TestLogEventCount(&logger_));
  TRY_TEST(TestLogElapsedTime(&logger_));
  TRY_TEST(TestLogFrameRate(&logger_));
  TRY_TEST(TestLogMemoryUsage(&logger_));
  TRY_TEST(TestLogIntHistogram(&logger_));
  TRY_TEST(TestLogCustomEvent(&logger_));
  TRY_TEST(TestLogCobaltEvent(&logger_));
  DropChild(std::move(child));

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

sys::testing::ScopedChild CobaltTestApp::Connect(const std::string &variant) {
  fuchsia::sys2::RealmSyncPtr realm_proxy;
  FX_CHECK(ZX_OK == context_->svc()->Connect(realm_proxy.NewRequest()))
      << "Failed to connect to fuchsia.sys2.Realm";

  auto child = sys::testing::ScopedChild::New(
      std::move(realm_proxy), "fuchsia_component_test_collection",
      "cobalt_under_test_" + std::to_string(scoped_child_destructors_.size()), variant);

  fuchsia::cobalt::LoggerFactorySyncPtr logger_factory =
      child.ConnectSync<fuchsia::cobalt::LoggerFactory>();
  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  uint32_t project_id =
      (test_for_prober_ ? cobalt_prober_registry::kProjectId : cobalt_registry::kProjectId);
  FX_LOGS(INFO) << "Test app is logging for the " << project_id << " project";
  logger_factory->CreateLoggerFromProjectId(project_id, logger_.logger_.NewRequest(), &status);
  FX_CHECK(status == fuchsia::cobalt::Status::OK) << "CreateLogger() => " << StatusToString(status);

  logger_factory->CreateLoggerSimpleFromProjectId(project_id, logger_.logger_simple_.NewRequest(),
                                                  &status);
  FX_CHECK(status == fuchsia::cobalt::Status::OK)
      << "CreateLoggerSimple() => " << StatusToString(status);

  fuchsia::metrics::MetricEventLoggerFactorySyncPtr metric_event_logger_factory =
      child.ConnectSync<fuchsia::metrics::MetricEventLoggerFactory>();

  fuchsia::metrics::Status metrics_status = fuchsia::metrics::Status::INTERNAL_ERROR;
  fuchsia::metrics::ProjectSpec project;
  project.set_customer_id(1);
  project.set_project_id(project_id);
  zx_status_t fx_status = metric_event_logger_factory->CreateMetricEventLogger(
      std::move(project), logger_.metric_event_logger_.NewRequest(), &metrics_status);
  FX_CHECK(fx_status == ZX_OK) << "FIDL: CreateMetricEventLogger() => " << fx_status;
  FX_CHECK(metrics_status == fuchsia::metrics::Status::OK)
      << "CreateMetricEventLogger() => " << StatusToString(metrics_status);

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
