// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This application is intenteded to be used for manual testing of
// the Cobalt encoder client on Fuchsia by Cobalt engineers.
//
// It also serves as an example of how to use the Cobalt FIDL API.
//
// It is also invoked by the cobalt_client CQ and CI.

#include <memory>
#include <string>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/cobalt_client/services/cobalt.fidl-sync.h"
#include "apps/cobalt_client/services/cobalt.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings_command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/tasks/message_loop.h"

// Command-line flags

// Don't use the network. Default=false (i.e. do use the network.)
constexpr ftl::StringView kNoNetworkForTesting = "no_network_for_testing";

// Number of observations in each batch. Default=7.
constexpr ftl::StringView kNumObservationsPerBatch =
    "num_observations_per_batch";

namespace {

const uint32_t kTestAppProjectId = 2;

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

std::string StatusToString(cobalt::Status status) {
  switch (status) {
    case cobalt::Status::OK:
      return "OK";
    case cobalt::Status::INVALID_ARGUMENTS:
      return "INVALID_ARGUMENTS";
    case cobalt::Status::SEND_FAILED:
      return "SEND_FAILED";
    case cobalt::Status::FAILED_PRECONDITION:
      return "FAILED_PRECONDITION";
    case cobalt::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
  }
};

class CobaltTestApp {
 public:
  CobaltTestApp(bool use_network, int num_observations_per_batch)
      : use_network_(use_network),
        num_observations_per_batch_(num_observations_per_batch),
        context_(app::ApplicationContext::CreateFromStartupInfo()) {}

  // Starts and connects to a Cobalt FIDL service.
  // Then invokes RunTests() three times. Returns true iff RunTests()
  // returns true all three times.
  bool ConnectAndRunTests();

 private:
  bool TestRareEventWithStrings() {
    return EncodeStringAndSend(kRareEventStringMetricId,
                               kRareEventStringEncodingId, kRareEvent1);
  }

  bool TestRareEventWithIndices() {
    for (uint32_t index : kRareEventIndicesToUse) {
      if (!EncodeIndexAndSend(kRareEventIndexMetricId,
                              kRareEventIndexEncodingId, index)) {
        return false;
      }
    }
    return true;
  }

  bool TestModuleUris() {
    return EncodeStringAndSend(kModuleViewsMetricId, kModuleViewsEncodingId,
                               kAModuleUri);
  }

  // Runs each of the tests until one fails. Returns true just in case they
  // all succeed.
  bool RunTests() {
    if (!TestRareEventWithStrings()) {
      return false;
    }
    if (!TestRareEventWithIndices()) {
      return false;
    }
    if (!TestModuleUris()) {
      return false;
    }
    return true;
  }

  // Synchronously invokes AddStringObservation() |num_observations_per_batch_|
  // times using the given parameters. Invokes SendObservations() if
  // |use_network_| is true. Returns true just in case everything succeeds.
  bool EncodeStringAndSend(uint32_t metric_id,
                           uint32_t encoding_config_id,
                           std::string val) {
    return EncodeAndSend(metric_id, encoding_config_id, false, val, 0);
  }

  // Synchronously invokes AddIndexObservation() |num_observations_per_batch_|
  // times using the given parameters. Invokes SendObservations() if
  // |use_network_| is true. Returns true just in case everything succeeds.
  bool EncodeIndexAndSend(uint32_t metric_id,
                          uint32_t encoding_config_id,
                          uint32_t index) {
    return EncodeAndSend(metric_id, encoding_config_id, true, "", index);
  }

  // Synchronously invokes either AddStringObservation() or
  // AddIndexObservation() (depending on the parameter |use_index|)
  // |num_observations_per_batch_| times using the given parameters. Invokes
  // SendObservations() if |use_network_| is true. Returns true just in case
  // everything succeeds.
  bool EncodeAndSend(uint32_t metric_id,
                     uint32_t encoding_config_id,
                     bool use_index,
                     std::string val,
                     uint32_t index);
  bool use_network_;
  int num_observations_per_batch_;
  std::unique_ptr<app::ApplicationContext> context_;
  app::ApplicationControllerPtr app_controller_;
  fidl::SynchronousInterfacePtr<cobalt::CobaltEncoder> encoder_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CobaltTestApp);
};

bool CobaltTestApp::EncodeAndSend(uint32_t metric_id,
                                  uint32_t encoding_config_id,
                                  bool use_index,
                                  std::string val,
                                  uint32_t index) {
  // Invoke AddStringObservation() multiple times.
  for (int i = 0; i < num_observations_per_batch_; i++) {
    cobalt::Status status = cobalt::Status::INTERNAL_ERROR;
    if (use_index) {
      encoder_->AddIndexObservation(metric_id, encoding_config_id, index,
                                    &status);
      FTL_LOG(INFO) << "AddIndex(" << index << ") => "
                    << StatusToString(status);
    } else {
      encoder_->AddStringObservation(metric_id, encoding_config_id, val,
                                     &status);
      FTL_LOG(INFO) << "AddString(" << val << ") => " << StatusToString(status);
    }
    if (status != cobalt::Status::OK) {
      return false;
    }
  }

  if (!use_network_) {
    FTL_LOG(INFO) << "Not using the network because --no_network_for_testing "
                     "was passed.";
    return true;
  }

  // Send the observations.
  cobalt::Status status = cobalt::Status::INTERNAL_ERROR;
  FTL_LOG(INFO) << "Invoking SendObservations() now...";
  encoder_->SendObservations(&status);
  FTL_LOG(INFO) << "SendObservations => " << StatusToString(status);
  return status == cobalt::Status::OK;
}

// Starts and connects to a Cobalt FIDL service.
// Then invokes RunTests() three times. Returns true iff RunTests()
// returns true all three times.
bool CobaltTestApp::ConnectAndRunTests() {
  // Start and connect to the cobalt fidl service.
  app::ServiceProviderPtr services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = "cobalt";
  launch_info->services = services.NewRequest();
  context_->launcher()->CreateApplication(std::move(launch_info),
                                          app_controller_.NewRequest());
  app_controller_.set_connection_error_handler([] {
    FTL_LOG(ERROR) << "Connection error from CobaltTestApp to CobaltClient.";
  });

  fidl::SynchronousInterfacePtr<cobalt::CobaltEncoderFactory> factory;
  app::ConnectToService(services.get(), fidl::GetSynchronousProxy(&factory));
  factory->GetEncoder(kTestAppProjectId, GetSynchronousProxy(&encoder_));

  // Invoke RunTests() three times and return true if it succeeds
  // all three times.
  for (int i = 0; i < 3; i++) {
    FTL_LOG(INFO) << "RunTests() iteration " << i << ".";
    if (!RunTests()) {
      return false;
    }
  }

  return true;
}

}  // namespace

int main(int argc, const char** argv) {
  const auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  ftl::SetLogSettingsFromCommandLine(command_line);
  bool use_network = !command_line.HasOption(kNoNetworkForTesting);
  auto num_observations_per_batch = std::stoi(
      command_line.GetOptionValueWithDefault(kNumObservationsPerBatch, "7"));

  mtl::MessageLoop loop;
  CobaltTestApp app(use_network, num_observations_per_batch);
  if (!app.ConnectAndRunTests()) {
    FTL_LOG(ERROR) << "FAIL";
    return 1;
  }
  FTL_LOG(INFO) << "PASS";
  return 0;
}
