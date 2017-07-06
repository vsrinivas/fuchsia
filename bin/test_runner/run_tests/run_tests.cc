// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple Fuchsia program that connects to the test_runner process,
// starts a test and exits with success or failure based on the success or
// failure of the test.

#include <launchpad/launchpad.h>
#include <magenta/processargs.h>
#include <magenta/status.h>

#include <iostream>

#include "apps/test_runner/lib/test_runner.h"
#include "apps/test_runner/src/run_tests/test_runner_config.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

namespace test_runner {
namespace {

// TODO(abarth): Remove this reference to a hard-coded config file.
constexpr char kModularTestsJson[] =
    "/system/apps/modular_tests/modular_tests.json";

class ModularTestRunObserver : public test_runner::TestRunObserver {
 public:
  ModularTestRunObserver(const std::string& test_id) : test_id_(test_id) {}
  void SendMessage(const std::string& test_id,
                   const std::string& operation,
                   const std::string& msg) override {
    FTL_CHECK(test_id == test_id_);
  }

  void Teardown(const std::string& test_id, bool success) override {
    FTL_CHECK(test_id == test_id_);
    success_ = success;
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  bool success() { return success_; }

 private:
  std::string test_id_;
  bool success_;
};

bool RunTest(std::shared_ptr<app::ApplicationContext> app_context,
             const std::string& url,
             const std::vector<std::string>& args) {
  uint64_t random_number;
  size_t random_size;
  mx_cprng_draw(&random_number, sizeof random_number, &random_size);
  std::string test_id = ftl::StringPrintf("test_%lX", random_number);
  ModularTestRunObserver observer(test_id);
  test_runner::TestRunContext context(app_context, &observer, test_id, url,
                                      args);

  mtl::MessageLoop::GetCurrent()->Run();

  return observer.success();
}

void PrintKnownTests(const TestRunnerConfig& config) {
  std::cerr << "Known tests are:" << std::endl;
  for (auto& test_name : config.test_names()) {
    std::cerr << " " << test_name << std::endl;
  }
}

int RunTestsMain(int argc, char** argv) {
  mtl::MessageLoop loop;
  ftl::CommandLine settings = ftl::CommandLineFromArgcArgv(argc, argv);
  TestRunnerConfig config(
      settings.GetOptionValueWithDefault("test_file", kModularTestsJson));

  if (settings.HasOption("help")) {
    std::cerr << R"USAGE(test runner [TEST NAME]
  --test_file <file path>    The json file defining all the tests. [DEFAULT:
                             /system/apps/modular_tests/modular_tests.json]
  --help                     This message.

  If a [TEST NAME] which is listed in --test_file is provided, it is run.
  Otherwise, all tests from --test_file are run.
)USAGE";

    PrintKnownTests(config);
    return 0;
  }

  std::shared_ptr<app::ApplicationContext> app_context =
      app::ApplicationContext::CreateFromStartupInfo();

  std::vector<std::string> test_names = settings.positional_args();
  if (test_names.empty()) {
    // If no tests were specified, run all tests.
    test_names = config.test_names();
  }

  std::vector<std::string> unknown;
  std::vector<std::string> failed;
  std::vector<std::string> succeeded;

  for (auto& test_name : test_names) {
    if (!config.HasTestNamed(test_name)) {
      unknown.push_back(test_name);
      continue;
    }
    std::vector<std::string> args =
        ftl::SplitStringCopy(config.GetTestCommand(test_name), " ",
                             ftl::kTrimWhitespace, ftl::kSplitWantNonEmpty);
    auto url = args.front();
    args.erase(args.begin());

    std::cerr << test_name << " ...\r";
    if (RunTest(app_context, url, args)) {
      std::cerr << test_name << " OK" << std::endl;
      succeeded.push_back(test_name);
    } else {
      std::cerr << test_name << " FAIL" << std::endl;
      failed.push_back(test_name);
    }
  }

  if (!succeeded.empty()) {
    std::cerr << "Succeeded tests:" << std::endl;
    for (auto& test_name : succeeded) {
      std::cerr << " " << test_name << std::endl;
    }
  }

  if (!failed.empty()) {
    std::cerr << "Failed tests:" << std::endl;
    for (auto& test_name : failed) {
      std::cerr << " " << test_name << std::endl;
    }
  }

  if (!unknown.empty()) {
    std::cerr << "Unknown tests:" << std::endl;
    for (auto& test_name : unknown) {
      std::cerr << " " << test_name << std::endl;
    }
    PrintKnownTests(config);
  }

  if (failed.empty() && unknown.empty()) {
    return 0;
  } else {
    return 1;
  }
}

}  // namespace
}  // namespace test_runner

int main(int argc, char** argv) {
  return test_runner::RunTestsMain(argc, argv);
}
