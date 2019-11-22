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
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <memory>
#include <sstream>
#include <string>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"
#include "src/cobalt/bin/testapp/prober_metrics_registry.cb.h"
#include "src/cobalt/bin/testapp/testapp_metrics_registry.cb.h"
#include "src/cobalt/bin/testapp/tests.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"
#include "src/lib/syslog/cpp/logger.h"

namespace cobalt {
namespace testapp {

// This must be less than 2^31. There appears to be a bug in
// std::condition_variable::wait_for() in which setting the wait time to
// std::chrono::seconds::max() effectively sets the wait time to zero.
constexpr uint32_t kInfiniteTime = 999999999;

#define TRY_TEST(test) \
  if (!(test)) {       \
    return false;      \
  }

#define CONNECT_AND_TRY_TEST(test, backfill_days)  \
  Connect(kInfiniteTime, 0, backfill_days, false); \
  if (!(test)) {                                   \
    return false;                                  \
  }

bool CobaltTestApp::RunTests() {
  // With the following values for the scheduling parameters we are
  // essentially configuring the ShippingManager to be in manual mode. It will
  // never send Observations because of the schedule and send them immediately
  // in response to RequestSendSoon().
  Connect(kInfiniteTime, 0);

  if (test_for_prober_) {
    TRY_TEST(CheckMetricIds());
  }

  // TODO(zmbush): Create tests for all logger methods.
  TRY_TEST(TestLogEvent(&logger_));
  TRY_TEST(TestLogEventCount(&logger_));
  TRY_TEST(TestLogElapsedTime(&logger_));
  TRY_TEST(TestLogFrameRate(&logger_));
  TRY_TEST(TestLogMemoryUsage(&logger_));
  TRY_TEST(TestLogIntHistogram(&logger_));
  TRY_TEST(TestLogCustomEvent(&logger_));
  TRY_TEST(TestLogCobaltEvent(&logger_));
  TRY_TEST(DoDebugMetricTest());

  if (!DoLocalAggregationTests(kEventAggregatorBackfillDays)) {
    return false;
  }

  return true;
}

void CobaltTestApp::SetChannel(const std::string &current_channel) {
  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  system_data_updater_->SetChannel(current_channel, &status);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK) << "Unable to set channel";
}

bool CobaltTestApp::DoDebugMetricTest() {
  // Currently Cobalt is hard-coded to always allow debug metrics.
  // Here we test that setting the channel (the mechanism that we used
  // to use to determine debug/non-debug) actually has no effect.
  bool should_succeed = true;
  SetChannel("prod");
  TRY_TEST(TestDebugMetric(&logger_, should_succeed, &cobalt_controller_));
  SetChannel("devhost");
  TRY_TEST(TestDebugMetric(&logger_, should_succeed, &cobalt_controller_));
  return true;
}

bool CobaltTestApp::DoLocalAggregationTests(const size_t backfill_days) {
  CONNECT_AND_TRY_TEST(
      TestLogEventWithAggregation(&logger_, clock_.get(), &cobalt_controller_, backfill_days),
      backfill_days);
  CONNECT_AND_TRY_TEST(
      TestLogEventCountWithAggregation(&logger_, clock_.get(), &cobalt_controller_, backfill_days),
      backfill_days);
  CONNECT_AND_TRY_TEST(
      TestLogElapsedTimeWithAggregation(&logger_, clock_.get(), &cobalt_controller_, backfill_days),
      backfill_days);
  return true;
}

void CobaltTestApp::Connect(uint32_t schedule_interval_seconds, uint32_t min_interval_seconds,
                            size_t event_aggregator_backfill_days,
                            bool start_event_aggregator_worker, uint32_t initial_interval_seconds) {
  controller_.Unbind();
  fidl::InterfaceHandle<fuchsia::io::Directory> directory;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "fuchsia-pkg://fuchsia.com/cobalt#meta/cobalt.cmx";
  launch_info.directory_request = directory.NewRequest().TakeChannel();
  launch_info.arguments.emplace();
  {
    std::ostringstream stream;
    stream << "--schedule_interval_seconds=" << schedule_interval_seconds;
    launch_info.arguments->push_back(stream.str());
  }

  if (initial_interval_seconds > 0) {
    std::ostringstream stream;
    stream << "--initial_interval_seconds=" << initial_interval_seconds;
    launch_info.arguments->push_back(stream.str());
  }

  {
    std::ostringstream stream;
    stream << "--min_interval_seconds=" << min_interval_seconds;
    launch_info.arguments->push_back(stream.str());
  }

  {
    std::ostringstream stream;
    stream << "--event_aggregator_backfill_days=" << event_aggregator_backfill_days;
    launch_info.arguments->push_back(stream.str());
  }

  {
    std::ostringstream stream;
    stream << "--start_event_aggregator_worker="
           << (start_event_aggregator_worker ? "true" : "false");
    launch_info.arguments->push_back(stream.str());
  }

  {
    std::ostringstream stream;
    stream << "--verbose=" << fxl::GetVlogVerbosity();
    launch_info.arguments->push_back(stream.str());
  }

  fuchsia::sys::LauncherPtr launcher;
  context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  controller_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Connection error from CobaltTestApp to Cobalt FIDL Service.";
  });

  sys::ServiceDirectory services(std::move(directory));

  // Delay to give the fake timekeeper service time to start and return that the clock is accurate.
  sleep(1);

  fuchsia::cobalt::LoggerFactorySyncPtr logger_factory;
  services.Connect(logger_factory.NewRequest());

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;

  uint32_t project_id =
      (test_for_prober_ ? cobalt_prober_registry::kProjectId : cobalt_registry::kProjectId);
  FX_LOGS(INFO) << "Test app is logging for the " << project_id << " project";
  logger_factory->CreateLoggerFromProjectId(project_id, logger_.logger_.NewRequest(), &status);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK)
      << "CreateLogger() => " << StatusToString(status);

  logger_factory->CreateLoggerSimpleFromProjectId(project_id, logger_.logger_simple_.NewRequest(),
                                                  &status);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK)
      << "CreateLoggerSimple() => " << StatusToString(status);

  services.Connect(system_data_updater_.NewRequest());
  SetChannel("devhost");

  services.Connect(cobalt_controller_.NewRequest());
}

}  // namespace testapp
}  // namespace cobalt
