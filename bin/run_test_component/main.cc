// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/termination_reason.h>
#include <stdio.h>

#include <fuchsia/sys/cpp/fidl.h>

#include "lib/component/cpp/environment_services.h"
#include "lib/component/cpp/testing/enclosing_environment.h"
#include "lib/component/cpp/testing/test_util.h"

using fuchsia::sys::TerminationReason;

constexpr char kEnv[] = "env_for_test";
// TODO(anmittal): Make it easier for developer to run tests.
//
// This should also support running tests using
//    package_name:test_name
//    package_name # where package_name and test_name are same.
int main(int argc, const char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: run_test_component <test_url><arguments>*\n");
    return 1;
  }
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = argv[1];
  std::string program_name = argv[1];

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  for (int i = 2; i < argc; i++) {
    launch_info.arguments.push_back(argv[i]);
  }

  fuchsia::sys::EnvironmentPtr parent_env;
  component::ConnectToEnvironmentService(parent_env.NewRequest());

  auto enclosing_env =
      component::testing::EnclosingEnvironment::Create(kEnv, parent_env);

  fuchsia::sys::ComponentControllerPtr controller;
  enclosing_env->CreateComponent(std::move(launch_info),
                                 controller.NewRequest());

  controller.events().OnTerminated = [&program_name](
                                         int64_t return_code,
                                         TerminationReason termination_reason) {
    if (termination_reason != TerminationReason::EXITED) {
      fprintf(stderr, "%s: %s\n", program_name.c_str(),
              component::HumanReadableTerminationReason(termination_reason)
                  .c_str());
    }
    zx_process_exit(return_code);
  };

  loop.Run();
  return 0;
}
