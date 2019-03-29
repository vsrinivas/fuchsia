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

#include <memory>
#include <sstream>
#include <string>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fsl/vmo/file.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"
#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/testapp/cobalt_testapp_logger.h"
#include "src/cobalt/bin/testapp/tests.h"

namespace cobalt {
namespace testapp {

// This app is not launched through appmgr as part of a package so we need the
// full path
constexpr char kLegacyConfigBinProtoPath[] =
    "/pkg/data/legacy_cobalt_metrics.pb";

constexpr char kConfigBinProtoPath[] = "/pkg/data/cobalt_metrics.pb";

fuchsia::cobalt::ProjectProfile CobaltTestApp::LoadCobaltConfig(
    CobaltConfigType type) {
  fsl::SizedVmo config_vmo;
  auto path = "";
  switch (type) {
    case kLegacyCobaltConfig:
      path = kLegacyConfigBinProtoPath;
      break;
    case kCobaltConfig:
      path = kConfigBinProtoPath;
      break;
  }
  bool success = fsl::VmoFromFilename(path, &config_vmo);
  FXL_CHECK(success) << "Could not read Cobalt config file into VMO";

  fuchsia::cobalt::ProjectProfile profile;
  profile.config = std::move(config_vmo).ToTransport();
  return profile;
}

bool CobaltTestApp::RunTests() {
  if (!RunTestsWithRequestSendSoon()) {
    return false;
  }
  if (!RunTestsWithBlockUntilEmpty()) {
    return false;
  }
  if (do_environment_test_) {
    return RunTestsUsingServiceFromEnvironment();
  } else {
    FXL_LOG(INFO) << "Skipping RunTestsUsingServiceFromEnvironment because "
                     "--skip_environment_test was passed.";
  }
  return true;
}

void CobaltTestApp::Connect(uint32_t schedule_interval_seconds,
                            uint32_t min_interval_seconds,
                            CobaltConfigType type,
                            bool start_event_aggregator_worker,
                            uint32_t initial_interval_seconds) {
  controller_.Unbind();
  fidl::InterfaceHandle<fuchsia::io::Directory> directory;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "fuchsia-pkg://fuchsia.com/cobalt#meta/cobalt.cmx";
  launch_info.directory_request = directory.NewRequest().TakeChannel();
  {
    std::ostringstream stream;
    stream << "--schedule_interval_seconds=" << schedule_interval_seconds;
    launch_info.arguments.push_back(stream.str());
  }

  if (initial_interval_seconds > 0) {
    std::ostringstream stream;
    stream << "--initial_interval_seconds=" << initial_interval_seconds;
    launch_info.arguments.push_back(stream.str());
  }

  {
    std::ostringstream stream;
    stream << "--min_interval_seconds=" << min_interval_seconds;
    launch_info.arguments.push_back(stream.str());
  }

  {
    std::ostringstream stream;
    stream << "--start_event_aggregator_worker="
           << start_event_aggregator_worker;
    launch_info.arguments.push_back(stream.str());
  }

  {
    std::ostringstream stream;
    stream << "--verbose=" << fxl::GetVlogVerbosity();
    launch_info.arguments.push_back(stream.str());
  }
  fuchsia::sys::LauncherPtr launcher;
  context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  controller_.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "Connection error from CobaltTestApp to CobaltClient.";
  });

  sys::ServiceDirectory services(std::move(directory));

  fuchsia::cobalt::LoggerFactorySyncPtr logger_factory;
  services.Connect(logger_factory.NewRequest());

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  logger_factory->CreateLogger(LoadCobaltConfig(type),
                               logger_.logger_.NewRequest(), &status);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK)
      << "CreateLogger() => " << StatusToString(status);

  logger_factory->CreateLoggerSimple(
      LoadCobaltConfig(type), logger_.logger_simple_.NewRequest(), &status);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK)
      << "CreateLoggerSimple() => " << StatusToString(status);

  services.Connect(cobalt_controller_.NewRequest());
}

bool CobaltTestApp::RunTestsWithRequestSendSoon() {
  // With the following values for the scheduling parameters we are
  // essentially configuring the ShippingManager to be in manual mode. It will
  // never send Observations because of the schedule and send them immediately
  // in response to RequestSendSoon().
  Connect(999999999, 0, kLegacyCobaltConfig);

  // Invoke LegacyRequestSendSoonTests() three times and return true if it
  // succeeds all three times.
  for (int i = 0; i < 3; i++) {
    FXL_LOG(INFO) << "\nRunTestsWithRequestSendSoon (legacy config) iteration "
                  << i << ".";
    if (!LegacyRequestSendSoonTests()) {
      return false;
    }
  }

  // Reconnect using the cobalt 1.0 config.
  Connect(999999999, 0, kCobaltConfig);
  FXL_LOG(INFO) << "\nRunTestsWithRequestSendSoon (v1.0 config) iteration.";
  if (!RequestSendSoonTests()) {
    return false;
  }

  return true;
}

bool CobaltTestApp::RunTestsWithBlockUntilEmpty() {
  Connect(1, 0, kLegacyCobaltConfig);

  // Invoke TestLogStringUsingBlockUntilEmpty() three times and
  // return true if it succeeds all three times.
  for (int i = 0; i < 3; i++) {
    FXL_LOG(INFO) << "\nRunTestsWithBlockUntilEmpty iteration " << i << ".";
    if (!legacy::TestLogStringUsingBlockUntilEmpty(&logger_)) {
      return false;
    }
  }

  return true;
}

bool CobaltTestApp::RunTestsUsingServiceFromEnvironment() {
  // Connect to the Cobalt FIDL service provided by the environment.
  fuchsia::cobalt::LoggerFactorySyncPtr logger_factory;
  zx_status_t zs = context_->svc()->Connect(logger_factory.NewRequest());
  FXL_CHECK(zs == ZX_OK);

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  zs = logger_factory->CreateLogger(LoadCobaltConfig(kLegacyCobaltConfig),
                                    logger_.logger_.NewRequest(), &status);
  FXL_CHECK(zs == ZX_OK);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK)
      << "CreateLogger() => " << StatusToString(status);

  logger_factory->CreateLoggerSimple(LoadCobaltConfig(kLegacyCobaltConfig),
                                     logger_.logger_simple_.NewRequest(),
                                     &status);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK)
      << "CreateLoggerSimple() => " << StatusToString(status);

  // Invoke TestLogEventUsingServiceFromEnvironment() three times
  // and return true if it succeeds all three times.
  for (int i = 0; i < 3; i++) {
    FXL_LOG(INFO) << "\nRunTestsUsingServiceFromEnvironment iteration " << i
                  << ".";
    // TODO(zmbush): Add a test from environment for v1.0
    if (!legacy::TestLogEventUsingServiceFromEnvironment(&logger_)) {
      return false;
    }
  }

  return true;
}

#define TRY_TEST(test) \
  if (!(test)) {       \
    return false;      \
  }

bool CobaltTestApp::LegacyRequestSendSoonTests() {
  TRY_TEST(legacy::TestLogEvent(&logger_));
  TRY_TEST(legacy::TestLogEventCount(&logger_));
  TRY_TEST(legacy::TestLogElapsedTime(&logger_));
  TRY_TEST(legacy::TestLogFrameRate(&logger_));
  TRY_TEST(legacy::TestLogMemoryUsage(&logger_));
  TRY_TEST(legacy::TestLogString(&logger_));
  TRY_TEST(legacy::TestLogTimer(&logger_));
  TRY_TEST(legacy::TestLogIntHistogram(&logger_));
  TRY_TEST(legacy::TestLogCustomEvent(&logger_));
  return true;
}

bool CobaltTestApp::RequestSendSoonTests() {
  // TODO(zmbush): Create tests for all logger methods (as we have for legacy).
  TRY_TEST(TestLogEvent(&logger_));
  TRY_TEST(TestLogEventCount(&logger_));
  TRY_TEST(TestLogElapsedTime(&logger_));
  TRY_TEST(TestLogFrameRate(&logger_));
  TRY_TEST(TestLogMemoryUsage(&logger_));
  TRY_TEST(TestLogIntHistogram(&logger_));
  TRY_TEST(TestLogCustomEvent(&logger_));
  return true;
}

}  // namespace testapp
}  // namespace cobalt
