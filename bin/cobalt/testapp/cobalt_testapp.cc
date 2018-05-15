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
#include "garnet/bin/cobalt/testapp/tests.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"

namespace cobalt {
namespace testapp {

using fuchsia::cobalt::Status2;

// This app is not launched through appmgr as part of a package so we need the
// full path
constexpr char kConfigBinProtoPath[] =
    "/pkgfs/packages/cobalt_tests/0/data/cobalt_config.binproto";

const uint32_t kTestAppProjectId = 2;

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
  factory->GetEncoder(kTestAppProjectId, encoder_.encoder_.NewRequest());

  fuchsia::cobalt::LoggerFactorySyncPtr logger_factory;
  services.ConnectToService(logger_factory.NewRequest());

  fuchsia::cobalt::Status2 status2 = fuchsia::cobalt::Status2::INTERNAL_ERROR;
  logger_factory->CreateLogger(LoadCobaltConfig2(), logger_.NewRequest(),
                               &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLogger() => " << StatusToString(status2);

  logger_factory->CreateLoggerExt(LoadCobaltConfig2(), logger_ext_.NewRequest(),
                                  &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLoggerExt() => " << StatusToString(status2);

  logger_factory->CreateLoggerSimple(LoadCobaltConfig2(),
                                     logger_simple_.NewRequest(), &status2);
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
  logger_factory->CreateLogger(LoadCobaltConfig2(), logger_.NewRequest(),
                               &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLogger() => " << StatusToString(status2);

  logger_factory->CreateLoggerExt(LoadCobaltConfig2(), logger_ext_.NewRequest(),
                                  &status2);
  FXL_CHECK(status2 == fuchsia::cobalt::Status2::OK)
      << "CreateLoggerExt() => " << StatusToString(status2);

  logger_factory->CreateLoggerSimple(LoadCobaltConfig2(),
                                     logger_simple_.NewRequest(), &status2);
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
  if (!TestLogEvent()) {
    return false;
  }
  if (!TestLogEventCount()) {
    return false;
  }
  if (!TestLogElapsedTime()) {
    return false;
  }
  if (!TestLogFrameRate()) {
    return false;
  }
  if (!TestLogMemoryUsage()) {
    return false;
  }
  if (!TestLogString()) {
    return false;
  }
  if (!TestLogTimer()) {
    return false;
  }
  if (!TestLogCustomEvent()) {
    return false;
  }
  return true;
}

bool CobaltTestApp::TestLogEvent() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEvent";
  bool use_request_send_soon = true;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!LogEventAndSend(kRareEventIndexMetricId, index,
                         use_request_send_soon)) {
      FXL_LOG(INFO) << "TestLogEvent: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "TestLogEvent: PASS";
  return true;
}

bool CobaltTestApp::TestLogEventCount() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogEventCount";
  bool use_request_send_soon = true;
  bool success =
      LogEventCountAndSend(kEventInComponentMetricId, kEventInComponentIndex,
                           kEventInComponentName, 1, use_request_send_soon);

  FXL_LOG(INFO) << "TestLogEventCount : " << (success ? "PASS" : "FAIL");
  return true;
}

bool CobaltTestApp::TestLogElapsedTime() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogElapsedTime";
  bool use_request_send_soon = true;
  bool success = LogElapsedTimeAndSend(
      kElapsedTimeMetricId, kElapsedTimeEventIndex, kElapsedTimeComponent,
      kElapsedTime, use_request_send_soon);
  success =
      success && LogElapsedTimeAndSend(kModTimerMetricId, 0, "",
                                       kModEndTimestamp - kModStartTimestamp,
                                       use_request_send_soon);
  FXL_LOG(INFO) << "TestLogElapsedTime : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestLogFrameRate() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogFrameRate";
  bool use_request_send_soon = true;
  bool success = LogFrameRateAndSend(kFrameRateMetricId, kFrameRateComponent,
                                     kFrameRate, use_request_send_soon);

  FXL_LOG(INFO) << "TestLogFrameRate : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestLogMemoryUsage() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogMemoryUsage";
  bool use_request_send_soon = true;
  bool success = LogMemoryUsageAndSend(kMemoryUsageMetricId, kMemoryUsageIndex,
                                       kMemoryUsage, use_request_send_soon);

  FXL_LOG(INFO) << "TestLogFrameRate : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestLogString() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogString";
  bool use_request_send_soon = true;
  bool success = LogStringAndSend(kRareEventStringMetricId, kRareEvent1,
                                  use_request_send_soon);
  FXL_LOG(INFO) << "TestLogString : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestLogTimer() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogTimer";
  bool use_request_send_soon = true;
  bool success =
      LogTimerAndSend(kModTimerMetricId, kModStartTimestamp, kModEndTimestamp,
                      kModTimerId, kModTimeout, use_request_send_soon);
  FXL_LOG(INFO) << "TestLogTimer : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestLogCustomEvent() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestLogCustomEvent";
  bool use_request_send_soon = true;
  bool success = LogStringPairAndSend(
      kModulePairsMetricId, kExistingModulePartName, kModulePairsEncodingId,
      "ModA", kAddedModulePartName, kModulePairsEncodingId, "ModB",
      use_request_send_soon);
  FXL_LOG(INFO) << "TestLogCustomEvent : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::LogEventAndSend(uint32_t metric_id, uint32_t index,
                                    bool use_request_send_soon) {
  for (int i = 0; i < encoder_.num_observations_per_batch_; i++) {
    Status2 status = Status2::INTERNAL_ERROR;
    logger_->LogEvent(metric_id, index, &status);
    FXL_VLOG(1) << "LogEvent(" << index << ") => " << StatusToString(status);
    if (status != Status2::OK) {
      FXL_LOG(ERROR) << "LogEvent() => " << StatusToString(status);
      return false;
    }
  }

  return encoder_.CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::LogEventCountAndSend(uint32_t metric_id, uint32_t index,
                                         const std::string& component,
                                         uint32_t count,
                                         bool use_request_send_soon) {
  for (int i = 0; i < encoder_.num_observations_per_batch_; i++) {
    Status2 status = Status2::INTERNAL_ERROR;
    logger_->LogEventCount(metric_id, index, component, 0, count, &status);
    FXL_VLOG(1) << "LogEventCount(" << index << ") => "
                << StatusToString(status);
    if (status != Status2::OK) {
      FXL_LOG(ERROR) << "LogEventCount() => " << StatusToString(status);
      return false;
    }
  }

  return encoder_.CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::LogElapsedTimeAndSend(uint32_t metric_id, uint32_t index,
                                          const std::string& component,
                                          int64_t elapsed_micros,
                                          bool use_request_send_soon) {
  for (int i = 0; i < encoder_.num_observations_per_batch_; i++) {
    Status2 status = Status2::INTERNAL_ERROR;
    logger_->LogElapsedTime(metric_id, index, component, elapsed_micros,
                            &status);
    FXL_VLOG(1) << "LogElapsedTime() => " << StatusToString(status);
    if (status != Status2::OK) {
      FXL_LOG(ERROR) << "LogElapsedTime() => " << StatusToString(status);
      return false;
    }
  }

  return encoder_.CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::LogFrameRateAndSend(uint32_t metric_id,
                                        const std::string& component, float fps,
                                        bool use_request_send_soon) {
  for (int i = 0; i < encoder_.num_observations_per_batch_; i++) {
    Status2 status = Status2::INTERNAL_ERROR;
    logger_->LogFrameRate(metric_id, 0, component, fps, &status);
    FXL_VLOG(1) << "LogFrameRate() => " << StatusToString(status);
    if (status != Status2::OK) {
      FXL_LOG(ERROR) << "LogFrameRate() => " << StatusToString(status);
      return false;
    }
  }

  return encoder_.CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::LogMemoryUsageAndSend(uint32_t metric_id, uint32_t index,
                                          int64_t bytes,
                                          bool use_request_send_soon) {
  for (int i = 0; i < encoder_.num_observations_per_batch_; i++) {
    Status2 status = Status2::INTERNAL_ERROR;
    logger_->LogMemoryUsage(metric_id, index, "", bytes, &status);
    FXL_VLOG(1) << "LogMemoryUsage) => " << StatusToString(status);
    if (status != Status2::OK) {
      FXL_LOG(ERROR) << "LogMemoryUsage() => " << StatusToString(status);
      return false;
    }
  }

  return encoder_.CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::LogStringAndSend(uint32_t metric_id, const std::string& val,
                                     bool use_request_send_soon) {
  for (int i = 0; i < encoder_.num_observations_per_batch_; i++) {
    Status2 status = Status2::INTERNAL_ERROR;
    logger_->LogString(metric_id, val, &status);
    FXL_VLOG(1) << "LogString(" << val << ") => " << StatusToString(status);
    if (status != Status2::OK) {
      FXL_LOG(ERROR) << "LogString() => " << StatusToString(status);
      return false;
    }
  }

  return encoder_.CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::LogTimerAndSend(uint32_t metric_id, uint32_t start_time,
                                    uint32_t end_time,
                                    const std::string& timer_id,
                                    uint32_t timeout_s,
                                    bool use_request_send_soon) {
  for (int i = 0; i < encoder_.num_observations_per_batch_; i++) {
    Status2 status = Status2::INTERNAL_ERROR;
    logger_->StartTimer(metric_id, 0, "", timer_id, start_time, timeout_s,
                        &status);
    logger_->EndTimer(timer_id, end_time, timeout_s, &status);

    FXL_VLOG(1) << "LogTimer("
                << "timer_id:" << timer_id << ", start_time:" << start_time
                << ", end_time:" << end_time << ") => "
                << StatusToString(status);
    if (status != Status2::OK) {
      FXL_LOG(ERROR) << "LogTimer() => " << StatusToString(status);
      return false;
    }
  }

  return encoder_.CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::LogStringPairAndSend(
    uint32_t metric_id, const std::string& part0, uint32_t encoding_id0,
    const std::string& val0, const std::string& part1, uint32_t encoding_id1,
    const std::string& val1, bool use_request_send_soon) {
  for (int i = 0; i < encoder_.num_observations_per_batch_; i++) {
    Status2 status = Status2::INTERNAL_ERROR;
    fidl::VectorPtr<fuchsia::cobalt::CustomEventValue> parts(2);
    parts->at(0).dimension_name = part0;
    parts->at(0).value.set_string_value(val0);
    parts->at(1).dimension_name = part1;
    parts->at(1).value.set_string_value(val1);
    logger_ext_->LogCustomEvent(metric_id, std::move(parts), &status);
    FXL_VLOG(1) << "LogCustomEvent(" << val0 << ", " << val1 << ") => "
                << StatusToString(status);
    if (status != Status2::OK) {
      FXL_LOG(ERROR) << "LogCustomEvent() => " << StatusToString(status);
      return false;
    }
  }

  return encoder_.CheckForSuccessfulSend(use_request_send_soon);
}

}  // namespace testapp
}  // namespace cobalt
