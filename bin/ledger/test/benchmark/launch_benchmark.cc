// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/benchmark/launch_benchmark.h"

#include <iostream>

#include "peridot/bin/ledger/test/benchmark/put/put.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/fxl/strings/string_number_conversions.h"

namespace {

constexpr fxl::StringView kAppUrlFlag = "app";

// Test argument and its values.
constexpr fxl::StringView kTestArgFlag = "test-arg";
constexpr fxl::StringView kMinValueFlag = "min-value";
constexpr fxl::StringView kMaxValueFlag = "max-value";
constexpr fxl::StringView kStepFlag = "step";
constexpr fxl::StringView kMultFlag = "mult";

constexpr fxl::StringView kAppendArgsFlag = "append-args";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kAppUrlFlag
            << "=<app url> --" << kTestArgFlag << "=<argument to test> --"
            << kMinValueFlag << "=<int> --" << kMaxValueFlag << "=<int> (--"
            << kStepFlag << "=<int>|" << kMultFlag << "=<int>) --"
            << kAppendArgsFlag << "=<extra arguments for the app>" << std::endl;
}

bool GetPositiveIntValue(const fxl::CommandLine& command_line,
                         fxl::StringView flag,
                         int* value) {
  std::string value_str;
  int found_value;
  if (!command_line.GetOptionValue(flag.ToString(), &value_str) ||
      !fxl::StringToNumberWithError(value_str, &found_value) ||
      found_value <= 0) {
    std::cerr << "Missing or invalid " << flag << " argument." << std::endl;
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
                                 SequenceType sequence_type,
                                 int step,
                                 std::vector<std::string> args)
    : app_url_(std::move(app_url)),
      test_arg_(std::move(test_arg)),
      current_value_(min_value),
      max_value_(max_value),
      sequence_type_(sequence_type),
      step_(step),
      args_(std::move(args)),
      context_(app::ApplicationContext::CreateFromStartupInfo()) {
  FXL_DCHECK(step > 0);
  FXL_DCHECK(max_value_ >= min_value);
}

void LaunchBenchmark::StartNext() {
  if (current_value_ > max_value_) {
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
    return;
  }

  app::ServiceProviderPtr child_services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = app_url_;
  launch_info->services = child_services.NewRequest();
  launch_info->arguments.push_back("--" + test_arg_ + "=" +
                                   fxl::NumberToString(current_value_));
  for (auto& arg : args_) {
    launch_info->arguments.push_back(arg);
  }
  context_->launcher()->CreateApplication(std::move(launch_info),
                                          GetProxy(&application_controller_));

  application_controller_.set_connection_error_handler([this] {
    switch (sequence_type_) {
      case SequenceType::ARITHMETIC:
        current_value_ += step_;
        break;
      case SequenceType::GEOMETRIC:
        current_value_ *= step_;
        break;
    }
    StartNext();
  });
}

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string app_url;
  if (!command_line.GetOptionValue(kAppUrlFlag.ToString(), &app_url)) {
    std::cerr << "Missing " << kAppUrlFlag << " argument." << std::endl;
    PrintUsage(argv[0]);
    return -1;
  }
  std::string test_arg;
  if (!command_line.GetOptionValue(kTestArgFlag.ToString(), &test_arg)) {
    std::cerr << "Missing " << kTestArgFlag << " argument." << std::endl;
    PrintUsage(argv[0]);
    return -1;
  }

  int min_value;
  int max_value;
  int step;
  if (!GetPositiveIntValue(command_line, kMinValueFlag, &min_value) ||
      !GetPositiveIntValue(command_line, kMaxValueFlag, &max_value)) {
    PrintUsage(argv[0]);
    return -1;
  }

  LaunchBenchmark::SequenceType sequence_type;
  std::string value_str;
  if (command_line.GetOptionValue(kStepFlag.ToString(), &value_str) ==
      command_line.GetOptionValue(kMultFlag.ToString(), &value_str)) {
    // Either both step and mult flags are given or they are both missing.
    std::cerr << "Exactly one of the " << kStepFlag << " or " << kMultFlag
              << " arguments must be provided." << std::endl;
    PrintUsage(argv[0]);
    return -1;
  }
  if (command_line.GetOptionValue(kStepFlag.ToString(), &value_str)) {
    sequence_type = LaunchBenchmark::SequenceType::ARITHMETIC;
    if (!GetPositiveIntValue(command_line, kStepFlag, &step)) {
      PrintUsage(argv[0]);
      return -1;
    }
  } else {
    sequence_type = LaunchBenchmark::SequenceType::GEOMETRIC;
    if (!GetPositiveIntValue(command_line, kMultFlag, &step)) {
      PrintUsage(argv[0]);
      return -1;
    }
  }

  if (max_value < min_value) {
    std::cerr << kMaxValueFlag << " should be >= " << kMinValueFlag
              << " (Found: " << max_value << " < " << min_value << ")";
    PrintUsage(argv[0]);
    return -1;
  }

  size_t index;
  std::vector<std::string> append_args;
  if (command_line.HasOption(kAppendArgsFlag, &index)) {
    append_args =
        fxl::SplitStringCopy(command_line.options()[index].value, ",",
                             fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  }

  fsl::MessageLoop loop;
  LaunchBenchmark launch_benchmark(std::move(app_url), std::move(test_arg),
                                   min_value, max_value, sequence_type, step,
                                   std::move(append_args));
  loop.task_runner()->PostTask(
      [&launch_benchmark] { launch_benchmark.StartNext(); });
  loop.Run();
  return 0;
}
