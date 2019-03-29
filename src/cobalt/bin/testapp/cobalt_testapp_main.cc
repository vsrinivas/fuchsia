// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This application is intenteded to be used for manual testing of
// the Cobalt logger client on Fuchsia by Cobalt engineers.
//
// It also serves as an example of how to use the Cobalt FIDL API.
//
// It is also invoked by the cobalt_client CQ and CI.

#include <memory>
#include <sstream>
#include <string>

#include <lib/async-loop/cpp/loop.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"
#include "src/cobalt/bin/testapp/cobalt_testapp.h"

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

int main(int argc, const char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);
  bool use_network = !command_line.HasOption(kNoNetworkForTesting);
  bool do_environment_test = !command_line.HasOption(kSkipEnvironmentTest);
  auto num_observations_per_batch = std::stoi(
      command_line.GetOptionValueWithDefault(kNumObservationsPerBatch, "7"));

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  cobalt::testapp::CobaltTestApp app(use_network, do_environment_test,
                                     num_observations_per_batch);
  if (!app.RunTests()) {
    FXL_LOG(ERROR) << "FAIL";
    return 1;
  }
  FXL_LOG(INFO) << "PASS";
  return 0;
}
