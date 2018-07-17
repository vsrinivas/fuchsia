// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This application is intenteded to be used for manual testing of
// the Cobalt encoder client on Fuchsia by Cobalt engineers.
//
// It also serves as an example of how to use the Cobalt FIDL API.
//
// It is also invoked by the cobalt_client CQ and CI.

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"

using fidl::VectorPtr;
using fuchsia::cobalt::Status;

// Command-line flags

// Don't use the network. Default=false (i.e. do use the network.)
constexpr fxl::StringView kNoNetworkForTesting = "no_network_for_testing";

// Number of observations in each batch. Default=7.
constexpr fxl::StringView kNumObservationsPerBatch =
    "num_observations_per_batch";

// Skip running the tests that use the service from the environment.
// We do this on the CQ and CI bots because they run with a special
// test environment instead of the standard Fuchsia application
// environment.
constexpr fxl::StringView kSkipEnvironmentTest = "skip_environment_test";

namespace {

// This app is not launched through appmgr as part of a package so we need the
// full path
constexpr char kConfigBinProtoPath[] =
    "/pkgfs/packages/cobalt_tests/0/data/cobalt_config.binproto";

// For the rare event with strings test
const uint32_t kRareEventStringMetricId = 1;
const uint32_t kRareEventStringEncodingId = 1;
const std::string kRareEvent1 = "Ledger-startup";

// For the module views test
const uint32_t kModuleViewsMetricId = 2;
const uint32_t kModuleViewsEncodingId = 2;
const std::string kAModuleUri = "www.cobalt_test_app.com";

// For the rare event with indexes test
const uint32_t kRareEventIndexMetricId = 3;
const uint32_t kRareEventIndexEncodingId = 3;
constexpr uint32_t kRareEventIndicesToUse[] = {0, 1, 2, 6};

// For the module pairs test
const uint32_t kModulePairsMetricId = 4;
const uint32_t kModulePairsEncodingId = 4;
const std::string kExistingModulePartName = "existing_module";
const std::string kAddedModulePartName = "added_module";

// For the num-stars-in-sky test
const uint32_t kNumStarsMetricId = 5;
const uint32_t kNumStarsEncodingId = 4;

// For the average-read-time test
const uint32_t kAvgReadTimeMetricId = 6;
const uint32_t kAvgReadTimeEncodingId = 4;

// For the spaceship velocity test.
const uint32_t kSpaceshipVelocityMetricId = 7;
const uint32_t kSpaceshipVelocityEncodingId = 4;

// For mod initialisation time.
const std::string kModTimerId = "test_mod_timer";
const uint32_t kModTimerMetricId = 8;
const uint32_t kModTimerEncodingId = 4;
const uint64_t kModStartTimestamp = 40;
const uint64_t kModEndTimestamp = 75;
const uint32_t kModTimeout = 1;

// For app startup time.
const std::string kAppTimerId = "test_app_timer";
const uint32_t kAppTimerMetricId = 9;
const uint32_t kAppTimerEncodingId = 4;
const std::string kAppTimerPartName = "time_ns";
const uint64_t kAppStartTimestamp = 10;
const uint64_t kAppEndTimestamp = 20;
const uint32_t kAppTimeout = 2;
const std::string kAppName = "hangouts";
const std::string kAppPartName = "app_name";
const uint32_t kAppNameEncodingId = 4;

// For testing V1_BACKEND.
const uint32_t kV1BackendMetricId = 10;
const uint32_t kV1BackendEncodingId = 4;
const std::string kV1BackendEvent = "Send-to-V1";

std::string StatusToString(Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case Status::OBSERVATION_TOO_BIG:
      return "OBSERVATION_TOO_BIG";
    case Status::TEMPORARILY_FULL:
      return "TEMPORARILY_FULL";
    case Status::SEND_FAILED:
      return "SEND_FAILED";
    case Status::FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
};

class CobaltTestApp {
 public:
  CobaltTestApp(bool use_network, bool do_environment_test,
                int num_observations_per_batch)
      : use_network_(use_network),
        do_environment_test_(do_environment_test),
        num_observations_per_batch_(num_observations_per_batch),
        context_(component::StartupContext::CreateFromStartupInfo()) {}

  // Loads the CobaltConfig proto for this project and returns it in a string
  std::string LoadCobaltConfig();

  // We have multiple testing strategies based on the method we use to
  // connect to the FIDL service and the method we use to determine whether
  // or not all of the sends to the Shuffler succeeded. This is the main
  // test function that invokes all of the strategies.
  bool RunAllTestingStrategies();

 private:
  // Starts and connects to the cobalt fidl service using the provided
  // scheduling parameters.
  void Connect(uint32_t schedule_interval_seconds,
               uint32_t min_interval_seconds);

  // Tests using the strategy of using the scheduling parameters (9999999, 0)
  // meaning that no scheduled sends will occur and RequestSendSoon() will cause
  // an immediate send so that we are effectively putting the ShippingManager
  // into a manual mode in which sends only occur when explicitly requested.
  // The tests invoke RequestSendSoon() when they want to send.
  bool RunTestsWithRequestSendSoon();

  // Tests using the strategy of initializing the ShippingManager with the
  // parameters (1, 0) meaning that scheduled sends will occur every second.
  // The test will then not invoke RequestSendSoon() but rather will add
  // some Observations and then invoke BlockUntilEmpty() and wait up to one
  // second for the sends to occur and then use the NumSendAttempts() and
  // FailedSendAttempts() accessors to determine success.
  bool RunTestsWithBlockUntilEmpty();

  // Tests using the instance of the Cobalt service found in the environment.
  // Since we do not construct the service we do not have the opportunity
  // to configure its scheduling parameters. For this reason we do not
  // wait for and verify a send to the Shuffler, we only verify that we
  // can successfully make FIDL calls
  bool RunTestsUsingServiceFromEnvironment();

  bool TestRareEventWithStrings();

  bool TestRareEventWithIndices();

  bool TestModuleUris();

  bool TestNumStarsInSky();

  bool TestSpaceshipVelocity();

  bool TestAvgReadTime();

  bool TestModulePairs();

  bool TestRareEventWithStringsUsingBlockUntilEmpty();

  bool TestRareEventWithIndicesUsingServiceFromEnvironment();

  bool TestModInitialisationTime();

  bool TestAppStartupTime();

  bool TestV1Backend();

  bool RequestSendSoonTests();

  // Synchronously invokes AddStringObservation() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool EncodeStringAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                           std::string val, bool use_request_send_soon);

  // Synchronously invokes AddIntObservation() |num_observations_per_batch_|
  // times using the given parameters.Then invokes CheckForSuccessfulSend().
  bool EncodeIntAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                        int32_t val, bool use_request_send_soon);

  // Synchronously invokes AddIntBucketDistribution()
  // |num_observations_per_batch_| times using the given parameters. Then
  // invokes CheckForSuccessfulSend().
  bool EncodeIntDistributionAndSend(
      uint32_t metric_id, uint32_t encoding_config_id,
      std::map<uint32_t, uint64_t> distribution_map,
      bool use_request_send_soon);

  // Synchronously invokes AddDoubleObservation() |num_observations_per_batch_|
  // times using the given parameters.Then invokes CheckForSuccessfulSend().
  bool EncodeDoubleAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                           double val, bool use_request_send_soon);

  // Synchronously invokes AddIndexObservation() |num_observations_per_batch_|
  // times using the given parameters. Then invokes CheckForSuccessfulSend().
  bool EncodeIndexAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                          uint32_t index, bool use_request_send_soon);

  // Synchronously invokes AddMultipartObservation() for an observation with
  // two string parts, |num_observations_per_batch_| times, using the given
  // parameters. Then invokes CheckForSuccessfulSend().
  bool EncodeStringPairAndSend(uint32_t metric_id, std::string part0,
                               uint32_t encoding_id0, std::string val0,
                               std::string part1, uint32_t encoding_id1,
                               std::string val1, bool use_request_send_soon);

  bool EncodeTimerAndSend(uint32_t metric_id, uint32_t encoding_config_id,
                          uint32_t start_time, uint32_t end_time,
                          std::string timer_id, uint32_t timeout_s,
                          bool use_request_send_soon);

  bool EncodeMultipartTimerAndSend(uint32_t metric_id, std::string part0,
                                   uint32_t encoding_id0, std::string val0,
                                   std::string part1, uint32_t encoding_id1,
                                   uint32_t start_time, uint32_t end_time,
                                   std::string timer_id, uint32_t timeout_s,
                                   bool use_request_send_soon);

  // If |use_network_| is false this method returns true immediately.
  // Otherwise, uses one of two strategies to cause the Observations that
  // have already been given to the Cobalt Client to be sent to the Shuffler
  // and then checks the status of the send. Returns true just in case the
  // send succeeds.
  //
  // |use_request_send_soon| specifies the strategy. If true then we
  // use the method RequestSendSoon() to ask the Cobalt Client to send the
  // Observations soon and return the status. Otherwise we use the method
  // BlockUntilEmpty() to wait for the CobaltClient to have sent all the
  // Observations it is holding and then we query NumSendAttempts() and
  // FailedSendAttempts().
  bool CheckForSuccessfulSend(bool use_request_send_soon);

  bool use_network_;
  bool do_environment_test_;
  int num_observations_per_batch_;
  int previous_value_of_num_send_attempts_ = 0;
  std::unique_ptr<component::StartupContext> context_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::cobalt::CobaltEncoderSyncPtr encoder_;
  fuchsia::cobalt::CobaltControllerSyncPtr cobalt_controller_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltTestApp);
};

std::string CobaltTestApp::LoadCobaltConfig() {
  // Open the cobalt config file.
  std::ifstream config_file_stream;
  config_file_stream.open(kConfigBinProtoPath);
  FXL_CHECK(config_file_stream && config_file_stream.good())
      << "Could not open the Cobalt config file: " << kConfigBinProtoPath;
  std::string cobalt_config_bytes;
  cobalt_config_bytes.assign(
      (std::istreambuf_iterator<char>(config_file_stream)),
      std::istreambuf_iterator<char>());
  FXL_CHECK(!cobalt_config_bytes.empty())
      << "Could not read the Cobalt config file: " << kConfigBinProtoPath;

  return cobalt_config_bytes;
}

bool CobaltTestApp::RunAllTestingStrategies() {
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

  fuchsia::cobalt::CobaltEncoderFactorySyncPtr factory;
  services.ConnectToService(factory.NewRequest());
  factory->GetEncoderForConfig(LoadCobaltConfig(), encoder_.NewRequest());

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
    if (!TestRareEventWithStringsUsingBlockUntilEmpty()) {
      return false;
    }
  }

  return true;
}

bool CobaltTestApp::RunTestsUsingServiceFromEnvironment() {
  // Connect to the Cobalt FIDL service provided by the environment.
  fuchsia::cobalt::CobaltEncoderFactorySyncPtr factory;
  context_->ConnectToEnvironmentService(factory.NewRequest());

  factory->GetEncoderForConfig(LoadCobaltConfig(), encoder_.NewRequest());

  // Invoke TestRareEventWithIndicesUsingServiceFromEnvironment() three times
  // and return true if it succeeds all three times.
  for (int i = 0; i < 3; i++) {
    FXL_LOG(INFO) << "\nRunTestsUsingServiceFromEnvironment iteration " << i
                  << ".";
    if (!TestRareEventWithIndicesUsingServiceFromEnvironment()) {
      return false;
    }
  }

  return true;
}

bool CobaltTestApp::RequestSendSoonTests() {
  if (!TestRareEventWithStrings()) {
    return false;
  }
  if (!TestRareEventWithIndices()) {
    return false;
  }
  if (!TestModuleUris()) {
    return false;
  }
  if (!TestNumStarsInSky()) {
    return false;
  }
  if (!TestSpaceshipVelocity()) {
    return false;
  }
  if (!TestAvgReadTime()) {
    return false;
  }
  if (!TestModulePairs()) {
    return false;
  }
  if (!TestModInitialisationTime()) {
    return false;
  }
  if (!TestAppStartupTime()) {
    return false;
  }
  if (!TestV1Backend()) {
    return false;
  }
  return true;
}

bool CobaltTestApp::TestRareEventWithStrings() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestRareEventWithStrings";
  bool use_request_send_soon = true;
  bool success =
      EncodeStringAndSend(kRareEventStringMetricId, kRareEventStringEncodingId,
                          kRareEvent1, use_request_send_soon);
  FXL_LOG(INFO) << "TestRareEventWithStrings : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestRareEventWithIndices() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestRareEventWithIndices";
  bool use_request_send_soon = true;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!EncodeIndexAndSend(kRareEventIndexMetricId, kRareEventIndexEncodingId,
                            index, use_request_send_soon)) {
      FXL_LOG(INFO) << "TestRareEventWithIndices: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "TestRareEventWithIndices: PASS";
  return true;
}

bool CobaltTestApp::TestModuleUris() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestModuleUris";
  bool use_request_send_soon = true;
  bool success =
      EncodeStringAndSend(kModuleViewsMetricId, kModuleViewsEncodingId,
                          kAModuleUri, use_request_send_soon);
  FXL_LOG(INFO) << "TestModuleUris : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestNumStarsInSky() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestNumStarsInSky";
  bool use_request_send_soon = true;
  bool success = EncodeIntAndSend(kNumStarsMetricId, kNumStarsEncodingId, 42,
                                  use_request_send_soon);
  FXL_LOG(INFO) << "TestNumStarsInSky : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestSpaceshipVelocity() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestSpaceshipVelocity";
  bool use_request_send_soon = true;
  std::map<uint32_t, uint64_t> distribution = {{1, 20}, {3, 20}};
  bool success = EncodeIntDistributionAndSend(
      kSpaceshipVelocityMetricId, kSpaceshipVelocityEncodingId, distribution,
      use_request_send_soon);
  FXL_LOG(INFO) << "TestSpaceshipVelocity : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestAvgReadTime() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestAvgReadTime";
  bool use_request_send_soon = true;
  bool success =
      EncodeDoubleAndSend(kAvgReadTimeMetricId, kAvgReadTimeEncodingId, 3.14159,
                          use_request_send_soon);
  FXL_LOG(INFO) << "TestAvgReadTime : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestModulePairs() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestModuleUriPairs";
  bool use_request_send_soon = true;
  bool success = EncodeStringPairAndSend(
      kModulePairsMetricId, kExistingModulePartName, kModulePairsEncodingId,
      "ModA", kAddedModulePartName, kModulePairsEncodingId, "ModB",
      use_request_send_soon);
  FXL_LOG(INFO) << "TestModuleUriPairs : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestRareEventWithStringsUsingBlockUntilEmpty() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestRareEventWithStringsUsingBlockUntilEmpty";
  bool use_request_send_soon = false;
  bool success =
      EncodeStringAndSend(kRareEventStringMetricId, kRareEventStringEncodingId,
                          kRareEvent1, use_request_send_soon);
  FXL_LOG(INFO) << "TestRareEventWithStringsUsingBlockUntilEmpty : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestRareEventWithIndicesUsingServiceFromEnvironment() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestRareEventWithIndicesUsingServiceFromEnvironment";
  // We don't actually use the network in this test strategy because we
  // haven't constructed the Cobalt service ourselves and so we haven't had
  // the opportunity to configure the scheduling parameters.
  bool save_use_network_value = use_network_;
  use_network_ = false;
  for (uint32_t index : kRareEventIndicesToUse) {
    if (!EncodeIndexAndSend(kRareEventIndexMetricId, kRareEventIndexEncodingId,
                            index, false)) {
      FXL_LOG(INFO)
          << "TestRareEventWithIndicesUsingServiceFromEnvironment: FAIL";
      return false;
    }
  }
  FXL_LOG(INFO) << "TestRareEventWithIndicesUsingServiceFromEnvironment: PASS";
  use_network_ = save_use_network_value;
  return true;
}

bool CobaltTestApp::TestModInitialisationTime() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestModInitialisationTime";
  bool use_request_send_soon = true;
  bool success = EncodeTimerAndSend(
      kModTimerMetricId, kModTimerEncodingId, kModStartTimestamp,
      kModEndTimestamp, kModTimerId, kModTimeout, use_request_send_soon);
  FXL_LOG(INFO) << "TestModInitialisationTime : "
                << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestAppStartupTime() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestAppStartupTime";
  bool use_request_send_soon = true;
  bool success = EncodeMultipartTimerAndSend(
      kAppTimerMetricId, kAppPartName, kAppNameEncodingId, kAppName,
      kAppTimerPartName, kAppTimerEncodingId, kAppStartTimestamp,
      kAppEndTimestamp, kAppTimerId, kAppTimeout, use_request_send_soon);
  FXL_LOG(INFO) << "TestAppStartupTime : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::TestV1Backend() {
  FXL_LOG(INFO) << "========================";
  FXL_LOG(INFO) << "TestV1Backend";
  bool use_request_send_soon = true;
  bool success = EncodeStringAndSend(kV1BackendMetricId, kV1BackendEncodingId,
                                     kV1BackendEvent, use_request_send_soon);
  FXL_LOG(INFO) << "TestV1Backend : " << (success ? "PASS" : "FAIL");
  return success;
}

bool CobaltTestApp::EncodeStringAndSend(uint32_t metric_id,
                                        uint32_t encoding_config_id,
                                        std::string val,
                                        bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_string_value(val);
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddStringObservation(metric_id, encoding_config_id, val,
                                     &status);
    }
    FXL_VLOG(1) << "AddStringObservation(" << val << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddStringObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::EncodeIntAndSend(uint32_t metric_id,
                                     uint32_t encoding_config_id, int32_t val,
                                     bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_int_value(val);
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddIntObservation(metric_id, encoding_config_id, val, &status);
    }
    FXL_VLOG(1) << "AddIntObservation(" << val << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddIntObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::EncodeIntDistributionAndSend(
    uint32_t metric_id, uint32_t encoding_config_id,
    std::map<uint32_t, uint64_t> distribution_map, bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    fidl::VectorPtr<fuchsia::cobalt::BucketDistributionEntry> distribution;
    for (auto it = distribution_map.begin(); distribution_map.end() != it;
         it++) {
      fuchsia::cobalt::BucketDistributionEntry entry;
      entry.index = it->first;
      entry.count = it->second;
      distribution.push_back(std::move(entry));
    }

    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_int_bucket_distribution(std::move(distribution));
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddIntBucketDistribution(metric_id, encoding_config_id,
                                         std::move(distribution), &status);
    }
    FXL_VLOG(1) << "AddIntBucketDistribution() => " << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddIntBucketDistribution() => "
                     << StatusToString(status);
      return false;
    }
  }

  FXL_LOG(INFO) << "About to Check!";
  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::EncodeDoubleAndSend(uint32_t metric_id,
                                        uint32_t encoding_config_id, double val,
                                        bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_double_value(val);
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddDoubleObservation(metric_id, encoding_config_id, val,
                                     &status);
    }
    FXL_VLOG(1) << "AddDoubleObservation(" << val << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddDoubleObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::EncodeIndexAndSend(uint32_t metric_id,
                                       uint32_t encoding_config_id,
                                       uint32_t index,
                                       bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    if (i == 0) {
      fuchsia::cobalt::Value value;
      value.set_index_value(index);
      encoder_->AddObservation(metric_id, encoding_config_id, std::move(value),
                               &status);
    } else {
      encoder_->AddIndexObservation(metric_id, encoding_config_id, index,
                                    &status);
    }
    FXL_VLOG(1) << "AddIndexObservation(" << index << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddIndexObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::EncodeTimerAndSend(uint32_t metric_id,
                                       uint32_t encoding_config_id,
                                       uint32_t start_time, uint32_t end_time,
                                       std::string timer_id, uint32_t timeout_s,
                                       bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    encoder_->StartTimer(metric_id, encoding_config_id, timer_id, start_time,
                         timeout_s, &status);
    encoder_->EndTimer(timer_id, end_time, timeout_s, &status);

    FXL_VLOG(1) << "AddTimerObservation("
                << "timer_id:" << timer_id << ", start_time:" << start_time
                << ", end_time:" << end_time << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddTimerObservation() => " << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::EncodeMultipartTimerAndSend(
    uint32_t metric_id, std::string part0, uint32_t encoding_id0,
    std::string val0, std::string part1, uint32_t encoding_id1,
    uint32_t start_time, uint32_t end_time, std::string timer_id,
    uint32_t timeout_s, bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    fidl::VectorPtr<fuchsia::cobalt::ObservationValue> parts(1);
    parts->at(0).name = part0;
    parts->at(0).encoding_id = encoding_id0;
    parts->at(0).value.set_string_value(val0);

    encoder_->StartTimer(metric_id, encoding_id1, timer_id, start_time,
                         timeout_s, &status);
    encoder_->EndTimerMultiPart(timer_id, end_time, part1, std::move(parts),
                                timeout_s, &status);

    FXL_VLOG(1) << "AddMultipartTimerObservation("
                << "timer_id:" << timer_id << ", start_time:" << start_time
                << ", end_time:" << end_time << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddMultipartTimerObservation() => "
                     << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::EncodeStringPairAndSend(
    uint32_t metric_id, std::string part0, uint32_t encoding_id0,
    std::string val0, std::string part1, uint32_t encoding_id1,
    std::string val1, bool use_request_send_soon) {
  for (int i = 0; i < num_observations_per_batch_; i++) {
    Status status = Status::INTERNAL_ERROR;
    fidl::VectorPtr<fuchsia::cobalt::ObservationValue> parts(2);
    parts->at(0).name = part0;
    parts->at(0).encoding_id = encoding_id0;
    parts->at(0).value.set_string_value(val0);
    parts->at(1).name = part1;
    parts->at(1).encoding_id = encoding_id1;
    parts->at(1).value.set_string_value(val1);
    encoder_->AddMultipartObservation(metric_id, std::move(parts), &status);
    FXL_VLOG(1) << "AddMultipartObservation(" << val0 << ", " << val1 << ") => "
                << StatusToString(status);
    if (status != Status::OK) {
      FXL_LOG(ERROR) << "AddMultipartObservation() => "
                     << StatusToString(status);
      return false;
    }
  }

  return CheckForSuccessfulSend(use_request_send_soon);
}

bool CobaltTestApp::CheckForSuccessfulSend(bool use_request_send_soon) {
  if (!use_network_) {
    FXL_LOG(INFO) << "Not using the network because --no_network_for_testing "
                     "was passed.";
    return true;
  }

  if (use_request_send_soon) {
    // Use the request-send-soon strategy to check the result of the send.
    bool send_success = false;
    FXL_VLOG(1) << "Invoking RequestSendSoon() now...";
    cobalt_controller_->RequestSendSoon(&send_success);
    FXL_VLOG(1) << "RequestSendSoon => " << send_success;
    return send_success;
  }

  // Use the block-until-empty strategy to check the result of the send.
  FXL_VLOG(1) << "Invoking BlockUntilEmpty(10)...";
  cobalt_controller_->BlockUntilEmpty(10);
  FXL_VLOG(1) << "BlockUntilEmpty() returned.";

  uint32_t num_send_attempts;
  cobalt_controller_->NumSendAttempts(&num_send_attempts);
  uint32_t failed_send_attempts;
  cobalt_controller_->FailedSendAttempts(&failed_send_attempts);
  FXL_VLOG(1) << "num_send_attempts=" << num_send_attempts;
  FXL_VLOG(1) << "failed_send_attempts=" << failed_send_attempts;
  uint32_t expected_lower_bound = previous_value_of_num_send_attempts_ + 1;
  previous_value_of_num_send_attempts_ = num_send_attempts;
  if (num_send_attempts < expected_lower_bound) {
    FXL_LOG(ERROR) << "num_send_attempts=" << num_send_attempts
                   << " expected_lower_bound=" << expected_lower_bound;
    return false;
  }
  if (failed_send_attempts != 0) {
    FXL_LOG(ERROR) << "failed_send_attempts=" << failed_send_attempts;
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);
  bool use_network = !command_line.HasOption(kNoNetworkForTesting);
  bool do_environment_test = !command_line.HasOption(kSkipEnvironmentTest);
  auto num_observations_per_batch = std::stoi(
      command_line.GetOptionValueWithDefault(kNumObservationsPerBatch, "7"));

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  CobaltTestApp app(use_network, do_environment_test,
                    num_observations_per_batch);
  if (!app.RunAllTestingStrategies()) {
    FXL_LOG(ERROR) << "FAIL";
    return 1;
  }
  FXL_LOG(INFO) << "PASS";
  return 0;
}
