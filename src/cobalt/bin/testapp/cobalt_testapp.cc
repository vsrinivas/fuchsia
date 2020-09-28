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
#include <lib/syslog/cpp/macros.h>

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
#include "src/cobalt/bin/utils/status_utils.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/macros.h"

namespace cobalt {
namespace testapp {

using ::cobalt::StatusToString;

// This must be less than 2^31. There appears to be a bug in
// std::condition_variable::wait_for() in which setting the wait time to
// std::chrono::seconds::max() effectively sets the wait time to zero.
constexpr uint32_t kInfiniteTime = 999999999;

#define TRY_TEST(test) \
  if (!(test)) {       \
    return false;      \
  }

#define CONNECT_AND_TRY_TEST(test, backfill_days)     \
  Connect(kInfiniteTime, 0, backfill_days, false, 0); \
  if (!(test)) {                                      \
    return false;                                     \
  }

#define CONNECT_AND_TRY_TEST_TWICE(test, backfill_days) \
  Connect(kInfiniteTime, 0, backfill_days, false, 0);   \
  if (!(test)) {                                        \
    Connect(kInfiniteTime, 0, backfill_days, false, 0); \
    if (!(test)) {                                      \
      return false;                                     \
    }                                                   \
  }

bool CobaltTestApp::RunTests() {
  // With the following values for the scheduling parameters we are
  // essentially configuring the ShippingManager to be in manual mode. It will
  // never send Observations because of the schedule and send them immediately
  // in response to RequestSendSoon().
  Connect(kInfiniteTime, 0, kEventAggregatorBackfillDays, true, 0);

  // TODO(zmbush): Create tests for all logger methods.
  TRY_TEST(TestLogEvent(&logger_));
  TRY_TEST(TestLogEventCount(&logger_));
  TRY_TEST(TestLogElapsedTime(&logger_));
  TRY_TEST(TestLogFrameRate(&logger_));
  TRY_TEST(TestLogMemoryUsage(&logger_));
  TRY_TEST(TestLogIntHistogram(&logger_));
  TRY_TEST(TestLogCustomEvent(&logger_));
  TRY_TEST(TestLogCobaltEvent(&logger_));

  if (!DoLocalAggregationTests(kEventAggregatorBackfillDays)) {
    return false;
  }

  return true;
}

bool CobaltTestApp::DoLocalAggregationTests(const size_t backfill_days) {
  // TODO(fxbug.dev/52750): We try each of these tests twice in case the failure
  // reason is that the calendar date has changed mid-test.
  CONNECT_AND_TRY_TEST_TWICE(
      TestLogEventWithAggregation(&logger_, clock_.get(), &cobalt_controller_, backfill_days),
      backfill_days);
  CONNECT_AND_TRY_TEST_TWICE(
      TestLogEventCountWithAggregation(&logger_, clock_.get(), &cobalt_controller_, backfill_days),
      backfill_days);
  CONNECT_AND_TRY_TEST_TWICE(
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
    stream << "--verbose=" << syslog::GetVlogVerbosity();
    launch_info.arguments->push_back(stream.str());
  }

  fuchsia::sys::LauncherPtr launcher;
  context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  controller_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Connection error from CobaltTestApp to Cobalt FIDL Service.";
  });

  sys::ServiceDirectory services(std::move(directory));

  fuchsia::cobalt::LoggerFactorySyncPtr logger_factory;
  services.Connect(logger_factory.NewRequest());

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

  services.Connect(system_data_updater_.NewRequest());
  status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  fuchsia::cobalt::SoftwareDistributionInfo info;
  info.set_current_channel("devhost");
  info.set_current_realm("");
  system_data_updater_->SetSoftwareDistributionInfo(std::move(info), &status);
  FX_CHECK(status == fuchsia::cobalt::Status::OK) << "Unable to set software distribution info";

  services.Connect(cobalt_controller_.NewRequest());

  // Block until the Cobalt service has been fully initialized. This includes
  // being notified by the timekeeper service that the system clock is accurate.
  FX_LOGS(INFO) << "Blocking until the Cobalt service is fully initialized.";
  cobalt_controller_->ListenForInitialized();
  FX_LOGS(INFO) << "Continuing because the Cobalt service is fully initialzied.";
}

}  // namespace testapp
}  // namespace cobalt
