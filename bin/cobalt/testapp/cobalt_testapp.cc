// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This application is intenteded to be used for manual testing of
// the Cobalt encoder client on Fuchsia by Cobalt engineers.
//
// It also serves as an example of how to use the Cobalt FIDL API.
//
// It is also invoked by the cobalt_client CQ and CI.

#include "garnet/bin/cobalt/testapp/cobalt_testapp.h"

#include <memory>
#include <sstream>
#include <string>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/cobalt/testapp/cobalt_testapp_encoder.h"
#include "garnet/bin/cobalt/testapp/cobalt_testapp_logger.h"
#include "garnet/bin/cobalt/testapp/tests.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"

namespace cobalt {
namespace testapp {

// This app is not launched through appmgr as part of a package so we need the
// full path
constexpr char kConfigBinProtoPath[] =
    "/pkgfs/packages/cobalt_tests/0/data/cobalt_config.binproto";

fuchsia::cobalt::ProjectProfile CobaltTestApp::LoadCobaltConfig() {
  fsl::SizedVmo config_vmo;
  bool success = fsl::VmoFromFilename(kConfigBinProtoPath, &config_vmo);
  FXL_CHECK(success) << "Could not read Cobalt config file into VMO";

  fuchsia::cobalt::ProjectProfile profile;
  fuchsia::mem::Buffer buf = std::move(config_vmo).ToTransport();
  profile.config.vmo = std::move(buf.vmo);
  profile.config.size = buf.size;
  return profile;
}

fuchsia::cobalt::ProjectProfile2 CobaltTestApp::LoadCobaltConfig2() {
  fsl::SizedVmo config_vmo;
  bool success = fsl::VmoFromFilename(kConfigBinProtoPath, &config_vmo);
  FXL_CHECK(success) << "Could not read Cobalt config file into VMO";

  fuchsia::cobalt::ProjectProfile2 profile;
  fuchsia::mem::Buffer buf = std::move(config_vmo).ToTransport();
  profile.config.vmo = std::move(buf.vmo);
  profile.config.size = buf.size;
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
                            uint32_t min_interval_seconds) {
  controller_.Unbind();
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "cobalt";
  launch_info.directory_request = services.NewRequest();
  {
    std::ostringstream stream;
    stream << "--schedule_interval_seconds=" << schedule_interval_seconds;
    launch_info.arguments.push_back(stream.str());
  }

  {
    std::ostringstream stream;
    stream << "--min_interval_seconds=" << min_interval_seconds;
    launch_info.arguments.push_back(stream.str());
  }

  {
    std::ostringstream stream;
    stream << "--verbose=" << fxl::GetVlogVerbosity();
    launch_info.arguments.push_back(stream.str());
  }
  context_->launcher()->CreateComponent(std::move(launch_info),
                                        controller_.NewRequest());
  controller_.set_error_handler([] {
    FXL_LOG(ERROR) << "Connection error from CobaltTestApp to CobaltClient.";
  });

  fuchsia::cobalt::EncoderFactorySyncPtr factory;
  services.ConnectToService(factory.NewRequest());

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;
  factory->GetEncoderForProject(LoadCobaltConfig(),
                                encoder_.encoder_.NewRequest(), &status);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK)
      << "GetEncoderForProject() => " << StatusToString(status);

  fuchsia::cobalt::LoggerFactorySyncPtr logger_factory;
  services.ConnectToService(logger_factory.NewRequest());

  fuchsia::cobalt::Status2 status2 = fuchsia::cobalt::Status2::INTERNAL_ERROR;
  logger_factory->CreateLogger(LoadCobaltConfig2(),
                               logger_.logger_.NewRequest(), &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLogger() => " << StatusToString(status2);

  logger_factory->CreateLoggerExt(LoadCobaltConfig2(),
                                  logger_.logger_ext_.NewRequest(), &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLoggerExt() => " << StatusToString(status2);

  logger_factory->CreateLoggerSimple(
      LoadCobaltConfig2(), logger_.logger_simple_.NewRequest(), &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLoggerSimple() => " << StatusToString(status2);

  services.ConnectToService(cobalt_controller_.NewRequest());
}

bool CobaltTestApp::RunTestsWithRequestSendSoon() {
  // With the following values for the scheduling parameters we are
  // essentially configuring the ShippingManager to be in manual mode. It will
  // never send Observations because of the schedule and send them immediately
  // in response to RequestSendSoon().
  Connect(999999999, 0);

  // Invoke RequestSendSoonTests() three times and return true if it
  // succeeds all three times.
  for (int i = 0; i < 3; i++) {
    FXL_LOG(INFO) << "\nRunTestsWithRequestSendSoon iteration " << i << ".";
    if (!RequestSendSoonTests()) {
      return false;
    }
  }

  return true;
}

bool CobaltTestApp::RunTestsWithBlockUntilEmpty() {
  Connect(1, 0);

  // Invoke TestRareEventWithStringsUsingBlockUntilEmpty() three times and
  // return true if it succeeds all three times.
  for (int i = 0; i < 3; i++) {
    FXL_LOG(INFO) << "\nRunTestsWithBlockUntilEmpty iteration " << i << ".";
    if (!TestRareEventWithStringsUsingBlockUntilEmpty(&encoder_)) {
      return false;
    }
  }

  return true;
}

bool CobaltTestApp::RunTestsUsingServiceFromEnvironment() {
  // Connect to the Cobalt FIDL service provided by the environment.
  fuchsia::cobalt::EncoderFactorySyncPtr factory;
  context_->ConnectToEnvironmentService(factory.NewRequest());

  fuchsia::cobalt::Status status = fuchsia::cobalt::Status::INTERNAL_ERROR;

  factory->GetEncoderForProject(LoadCobaltConfig(),
                                encoder_.encoder_.NewRequest(), &status);
  FXL_CHECK(status == fuchsia::cobalt::Status::OK)
      << "GetEncoderForProject() => " << StatusToString(status);

  fuchsia::cobalt::LoggerFactorySyncPtr logger_factory;
  context_->ConnectToEnvironmentService(logger_factory.NewRequest());

  fuchsia::cobalt::Status2 status2 = fuchsia::cobalt::Status2::INTERNAL_ERROR;
  logger_factory->CreateLogger(LoadCobaltConfig2(),
                               logger_.logger_.NewRequest(), &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLogger() => " << StatusToString(status2);

  logger_factory->CreateLoggerExt(LoadCobaltConfig2(),
                                  logger_.logger_ext_.NewRequest(), &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLoggerExt() => " << StatusToString(status2);

  logger_factory->CreateLoggerSimple(
      LoadCobaltConfig2(), logger_.logger_simple_.NewRequest(), &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLoggerSimple() => " << StatusToString(status2);

  // Invoke TestRareEventWithIndicesUsingServiceFromEnvironment() three times
  // and return true if it succeeds all three times.
  for (int i = 0; i < 3; i++) {
    FXL_LOG(INFO) << "\nRunTestsUsingServiceFromEnvironment iteration " << i
                  << ".";
    if (!TestRareEventWithIndicesUsingServiceFromEnvironment(&encoder_)) {
      return false;
    }
  }

  return true;
}

bool CobaltTestApp::RequestSendSoonTests() {
  if (!TestRareEventWithStrings(&encoder_)) {
    return false;
  }
  if (!TestRareEventWithIndices(&encoder_)) {
    return false;
  }
  if (!TestModuleUris(&encoder_)) {
    return false;
  }
  if (!TestNumStarsInSky(&encoder_)) {
    return false;
  }
  if (!TestSpaceshipVelocity(&encoder_)) {
    return false;
  }
  if (!TestAvgReadTime(&encoder_)) {
    return false;
  }
  if (!TestModulePairs(&encoder_)) {
    return false;
  }
  if (!TestModInitializationTime(&encoder_)) {
    return false;
  }
  if (!TestAppStartupTime(&encoder_)) {
    return false;
  }
  if (!TestV1Backend(&encoder_)) {
    return false;
  }
  if (!TestLogEvent(&logger_)) {
    return false;
  }
  if (!TestLogEventCount(&logger_)) {
    return false;
  }
  if (!TestLogElapsedTime(&logger_)) {
    return false;
  }
  if (!TestLogFrameRate(&logger_)) {
    return false;
  }
  if (!TestLogMemoryUsage(&logger_)) {
    return false;
  }
  if (!TestLogString(&logger_)) {
    return false;
  }
  if (!TestLogTimer(&logger_)) {
    return false;
  }
  if (!TestLogIntHistogram(&logger_)) {
    return false;
  }
  if (!TestLogCustomEvent(&logger_)) {
    return false;
  }
  return true;
}

}  // namespace testapp
}  // namespace cobalt
