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
const uint32_t kRareEventMetricId = 1;
const uint32_t kRareEventEncodingId = 1;
const std::string kRareEvent1 = "Ledger-startup";

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

  // Synchronously invokes AddStringObservation() |num_observations_per_batch_|
  // times. Invokes SendObservations() if |use_network_| is true. Returns true
  // just in case everything succeeds.
  bool RunTest() {
    // Add a batch of observations of kRareEvent1 to the envelope.
    for (int i = 0; i < num_observations_per_batch_; i++) {
      cobalt::Status status = cobalt::Status::INTERNAL_ERROR;
      encoder_->AddStringObservation(kRareEventMetricId, kRareEventEncodingId,
                                     kRareEvent1, &status);
      FTL_LOG(INFO) << "Add(kRareEvent1) => " << StatusToString(status);
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
  // Then invokes RunTest() three times. Returns true iff RunTest()
  // returns true all three times.
  bool ConnectAndRunTest() {
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

    // Invoke RunTest() three times and return true if it succeeds
    // all three times.
    for (int i = 0; i < 3; i++) {
      FTL_LOG(INFO) << "RunTest() iteration " << i << ".";
      if (!RunTest()) {
        return false;
      }
    }

    return true;
  }

 private:
  bool use_network_;
  int num_observations_per_batch_;
  std::unique_ptr<app::ApplicationContext> context_;
  app::ApplicationControllerPtr app_controller_;
  fidl::SynchronousInterfacePtr<cobalt::CobaltEncoder> encoder_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CobaltTestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  const auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  ftl::SetLogSettingsFromCommandLine(command_line);
  bool use_network = !command_line.HasOption(kNoNetworkForTesting);
  auto num_observations_per_batch = std::stoi(
      command_line.GetOptionValueWithDefault(kNumObservationsPerBatch, "7"));

  mtl::MessageLoop loop;
  CobaltTestApp app(use_network, num_observations_per_batch);
  if (!app.ConnectAndRunTest()) {
    FTL_LOG(ERROR) << "FAIL";
    return 1;
  }
  FTL_LOG(INFO) << "PASS";
  return 0;
}
