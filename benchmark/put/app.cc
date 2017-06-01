// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/ledger/benchmark/put/put.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr ftl::StringView kEntryCountFlag = "entry-count";
constexpr ftl::StringView kTransactionSizeFlag = "transaction-size";
constexpr ftl::StringView kKeySizeFlag = "key-size";
constexpr ftl::StringView kValueSizeFlag = "value-size";
constexpr ftl::StringView kUpdateFlag = "update";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kEntryCountFlag
            << "=<int> --" << kTransactionSizeFlag << "=<int> --"
            << kKeySizeFlag << "=<int> --" << kValueSizeFlag << "=<int> ["
            << kUpdateFlag << "]" << std::endl;
}

bool GetPositiveIntValue(const ftl::CommandLine& command_line,
                         ftl::StringView flag,
                         int* value) {
  std::string value_str;
  int found_value;
  if (!command_line.GetOptionValue(flag.ToString(), &value_str) ||
      !ftl::StringToNumberWithError(value_str, &found_value) ||
      found_value <= 0) {
    return false;
  }
  *value = found_value;
  return true;
}

}  // namespace

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  int entry_count;
  int transaction_size;
  int key_size;
  int value_size;
  bool update = command_line.HasOption(kUpdateFlag.ToString());
  if (!GetPositiveIntValue(command_line, kEntryCountFlag, &entry_count) ||
      !GetPositiveIntValue(command_line, kTransactionSizeFlag,
                           &transaction_size) ||
      !GetPositiveIntValue(command_line, kKeySizeFlag, &key_size) ||
      !GetPositiveIntValue(command_line, kValueSizeFlag, &value_size)) {
    PrintUsage(argv[0]);
    return -1;
  }

  mtl::MessageLoop loop;
  benchmark::PutBenchmark app(entry_count, transaction_size, key_size,
                              value_size, update);
  // TODO(nellyv): A delayed task is necessary because of US-257.
  loop.task_runner()->PostDelayedTask([&app] { app.Run(); },
                                      ftl::TimeDelta::FromSeconds(1));
  loop.Run();
  return 0;
}
