// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/ledger/benchmark/constants.h"
#include "apps/ledger/benchmark/put/put.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

// Test type.
constexpr ftl::StringView kEntryCountFlag = "entry-count";
constexpr ftl::StringView kTransactionSizeFlag = "transaction-size";
constexpr ftl::StringView kKeySizeFlag = "key-size";
constexpr ftl::StringView kValueSizeFlag = "value-size";

// Value arguments.
constexpr ftl::StringView kMinValueFlag = "min-value";
constexpr ftl::StringView kMaxValueFlag = "max-value";
constexpr ftl::StringView kStepFlag = "step";

// Optional argument.
constexpr ftl::StringView kUpdateFlag = "update";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " <test-type> --"
            << kMinValueFlag << "=<int> --" << kMaxValueFlag << "=<int> --"
            << kStepFlag << "=<int> [" << kUpdateFlag << "]" << std::endl;
  std::cout << "Where <test-type>: " << kEntryCountFlag << " | "
            << kTransactionSizeFlag << " | " << kKeySizeFlag << " | "
            << kValueSizeFlag << std::endl;
}

bool GetPositiveIntValue(const ftl::CommandLine& command_line,
                         ftl::StringView flag,
                         int* value) {
  std::string value_str;
  int found_value;
  if (!command_line.GetOptionValue(flag.ToString(), &value_str) ||
      !ftl::StringToNumberWithError(value_str, &found_value) ||
      found_value <= 0) {
    std::cout << "Missing or incorrect " << flag << " argument." << std::endl;
    return false;
  }
  *value = found_value;
  return true;
}

void Run(std::function<void(benchmark::PutBenchmark*, int)> update_value,
         int min_value,
         int max_value,
         int step,
         bool update) {
  mtl::MessageLoop loop;
  int value = min_value;
  benchmark::PutBenchmark app(kDefaultEntryCount, kDefaultTransactionSize,
                              kDefaultKeySize, kDefaultValueSize, update);
  update_value(&app, value);

  app.set_on_done([
    &app, update_value = std::move(update_value), &value, max_value, step
  ]() {
    if (value >= max_value) {
      app.ShutDown();
      return;
    }
    value += step;
    update_value(&app, value);
    app.Run();
  });
  loop.task_runner()->PostTask([&app] { app.Run(); });
  loop.Run();
}

}  // namespace

int main(int argc, const char** argv) {
  if (argc < 5 || argc > 6) {
    std::cout << "Incorrect number of arguments (" << argc << ")" << std::endl;
    PrintUsage(argv[0]);
    return -1;
  }
  std::function<void(benchmark::PutBenchmark*, int)> update_value;
  std::string type = argv[1];
  if (type == kEntryCountFlag) {
    update_value = [](benchmark::PutBenchmark* app, int value) {
      app->set_entry_count(value);
    };
  } else if (type == kTransactionSizeFlag) {
    update_value = [](benchmark::PutBenchmark* app, int value) {
      app->set_transaction_size(value);
    };
  } else if (type == kKeySizeFlag) {
    update_value = [](benchmark::PutBenchmark* app, int value) {
      app->set_key_size(value);
    };
  } else if (type == kValueSizeFlag) {
    update_value = [](benchmark::PutBenchmark* app, int value) {
      app->set_value_size(value);
    };
  } else {
    std::cout << "Incorrect test type: " << type << std::endl;
    PrintUsage(argv[0]);
    return -1;
  }

  ftl::CommandLine command_line =
      ftl::CommandLineFromArgcArgv(argc - 1, &argv[1]);
  bool update = command_line.HasOption(kUpdateFlag.ToString());
  int min_value;
  int max_value;
  int step;
  if (!GetPositiveIntValue(command_line, kMinValueFlag, &min_value) ||
      !GetPositiveIntValue(command_line, kMaxValueFlag, &max_value) ||
      !GetPositiveIntValue(command_line, kStepFlag, &step)) {
    PrintUsage(argv[0]);
    return -1;
  }

  Run(std::move(update_value), min_value, max_value, step, update);
  return 0;
}
