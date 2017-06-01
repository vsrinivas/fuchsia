// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/benchmark/launch_benchmark.h"

#include <iostream>

#include "apps/ledger/benchmark/put/put.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr ftl::StringView kAppUrlFlag = "app";

// Test argument and its values.
constexpr ftl::StringView kTestArgFlag = "test-arg";
constexpr ftl::StringView kMinValueFlag = "min-value";
constexpr ftl::StringView kMaxValueFlag = "max-value";
constexpr ftl::StringView kStepFlag = "step";

constexpr ftl::StringView kAppendArgsFlag = "append-args";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kAppUrlFlag
            << "=<app url> --" << kTestArgFlag << "=<argument to test> --"
            << kMinValueFlag << "=<int> --" << kMaxValueFlag << "=<int> --"
            << kStepFlag << "=<int> --" << kAppendArgsFlag
            << "<extra arguments for the app>" << std::endl;
}

bool GetPositiveIntValue(const ftl::CommandLine& command_line,
                         ftl::StringView flag,
                         int* value) {
  std::string value_str;
  int found_value;
  if (!command_line.GetOptionValue(flag.ToString(), &value_str) ||
      !ftl::StringToNumberWithError(value_str, &found_value) ||
      found_value <= 0) {
    std::cout << "Missing or invalid " << flag << " argument." << std::endl;
    return false;
  }
  *value = found_value;
  return true;
}

}  // namespace

LaunchBenchmark::LaunchBenchmark(std::string app_url,
                                 std::string test_arg,
                                 int min_value,
                                 int max_value,
                                 int step,
                                 std::vector<std::string> args)
    : app_url_(std::move(app_url)),
      test_arg_(std::move(test_arg)),
      current_value_(min_value),
      max_value_(max_value),
      step_(step),
      args_(std::move(args)),
      context_(app::ApplicationContext::CreateFromStartupInfo()) {
  FTL_DCHECK(step > 0);
  FTL_DCHECK(max_value_ >= min_value);
}

void LaunchBenchmark::StartNext() {
  if (current_value_ > max_value_) {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
    return;
  }

  app::ServiceProviderPtr child_services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = app_url_;
  launch_info->services = child_services.NewRequest();
  launch_info->arguments.push_back("--" + test_arg_ + "=" +
                                   ftl::NumberToString(current_value_));
  for (auto& arg : args_) {
    launch_info->arguments.push_back(arg);
  }
  context_->launcher()->CreateApplication(std::move(launch_info),
                                          GetProxy(&application_controller_));

  application_controller_.set_connection_error_handler([this] {
    current_value_ += step_;
    StartNext();
  });
}

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  std::string app_url;
  if (!command_line.GetOptionValue(kAppUrlFlag.ToString(), &app_url)) {
    std::cout << "Missing " << kAppUrlFlag << " argument." << std::endl;
    PrintUsage(argv[0]);
    return -1;
  }
  std::string test_arg;
  if (!command_line.GetOptionValue(kTestArgFlag.ToString(), &test_arg)) {
    std::cout << "Missing " << kTestArgFlag << " argument." << std::endl;
    PrintUsage(argv[0]);
    return -1;
  }

  int min_value;
  int max_value;
  int step;
  if (!GetPositiveIntValue(command_line, kMinValueFlag, &min_value) ||
      !GetPositiveIntValue(command_line, kMaxValueFlag, &max_value) ||
      !GetPositiveIntValue(command_line, kStepFlag, &step)) {
    PrintUsage(argv[0]);
    return -1;
  }
  if (max_value < min_value) {
    std::cout << kMaxValueFlag << " should be >= " << kMinValueFlag
              << " (Found: " << max_value << " < " << min_value << ")";
    PrintUsage(argv[0]);
    return -1;
  }

  size_t index;
  std::vector<std::string> append_args;
  if (command_line.HasOption(kAppendArgsFlag, &index)) {
    append_args =
        ftl::SplitStringCopy(command_line.options()[index].value, ",",
                             ftl::kTrimWhitespace, ftl::kSplitWantNonEmpty);
  }

  mtl::MessageLoop loop;
  LaunchBenchmark launch_benchmark(std::move(app_url), std::move(test_arg),
                                   min_value, max_value, step,
                                   std::move(append_args));
  loop.task_runner()->PostTask(
      [&launch_benchmark] { launch_benchmark.StartNext(); });
  loop.Run();
  return 0;
}
