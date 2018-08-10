// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/termination_reason.h>

#include "garnet/bin/run_test_component/run_test_component.h"
#include "lib/component/cpp/environment_services.h"
#include "lib/component/cpp/testing/enclosing_environment.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/fxl/strings/string_printf.h"

using fuchsia::sys::TerminationReason;

namespace {
constexpr char kEnv[] = "env_for_test";

void PrintUsage() {
  fprintf(stderr, R"(
Usage: run_test_component <test_url> [arguments...]
       run_test_component <test_prefix> [arguments...]

       *test_url* takes the form of component manifest URL which uniquely
       identifies a test component. Example:
          fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello.cmx

       if *test_prefix* is provided, this tool will glob over /pkgfs and look for
       matching cmx files. If multiple files are found, it will print
       corresponding component URLs and exit.  If there is only one match, it
       will generate a component URL and execute the test.

       example:
        run_test_component run_test_component_unit
          will match /pkgfs/packages/run_test_component_unittests/meta/run_test_component_unittests.cmx and run it.
)");
}

}  // namespace

int main(int argc, const char** argv) {
  auto parse_result = run::ParseArgs(argc, argv, "/pkgfs/packages");
  if (parse_result.error) {
    if (parse_result.error_msg != "") {
      fprintf(stderr, "%s\n", parse_result.error_msg.c_str());
    }
    PrintUsage();
    return 1;
  }
  if (parse_result.matching_urls.size() > 1) {
    fprintf(stderr, "Found multiple matching components. Did you mean?\n");
    for (auto url : parse_result.matching_urls) {
      fprintf(stderr, "%s\n", url.c_str());
    }
    return 1;
  } else if (parse_result.matching_urls.size() == 1) {
    fprintf(stdout, "Found one matching component. Running: %s\n",
            parse_result.matching_urls[0].c_str());
  }
  std::string program_name = parse_result.launch_info.url;

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fuchsia::sys::EnvironmentPtr parent_env;
  component::ConnectToEnvironmentService(parent_env.NewRequest());

  auto enclosing_env =
      component::testing::EnclosingEnvironment::Create(kEnv, parent_env);

  fuchsia::sys::ComponentControllerPtr controller;
  enclosing_env->CreateComponent(std::move(parse_result.launch_info),
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
