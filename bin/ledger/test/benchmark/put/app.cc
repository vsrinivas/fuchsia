// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <trace-provider/provider.h>

#include "apps/ledger/src/test/benchmark/put/put.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/random/rand.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr fxl::StringView kEntryCountFlag = "entry-count";
constexpr fxl::StringView kTransactionSizeFlag = "transaction-size";
constexpr fxl::StringView kKeySizeFlag = "key-size";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kRefsFlag = "refs";
constexpr fxl::StringView kUpdateFlag = "update";
constexpr fxl::StringView kSeedFlag = "seed";

constexpr fxl::StringView kRefsOnFlag = "on";
constexpr fxl::StringView kRefsOffFlag = "off";
constexpr fxl::StringView kRefsAutoFlag = "auto";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kEntryCountFlag
            << "=<int> --" << kTransactionSizeFlag << "=<int> --"
            << kKeySizeFlag << "=<int> --" << kValueSizeFlag << "=<int> --"
            << kRefsFlag << "=(" << kRefsOnFlag << "|" << kRefsOffFlag << "|"
            << kRefsAutoFlag << ") [" << kSeedFlag << "=<int>] [" << kUpdateFlag
            << "]" << std::endl;
}

bool GetPositiveIntValue(const fxl::CommandLine& command_line,
                         fxl::StringView flag,
                         int* value) {
  std::string value_str;
  int found_value;
  if (!command_line.GetOptionValue(flag.ToString(), &value_str) ||
      !fxl::StringToNumberWithError(value_str, &found_value) ||
      found_value <= 0) {
    return false;
  }
  *value = found_value;
  return true;
}

}  // namespace

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

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

  std::string ref_strategy_str;
  if (!command_line.GetOptionValue(kRefsFlag.ToString(), &ref_strategy_str)) {
    PrintUsage(argv[0]);
    return -1;
  }
  test::benchmark::PutBenchmark::ReferenceStrategy ref_strategy;
  if (ref_strategy_str == kRefsOnFlag) {
    ref_strategy = test::benchmark::PutBenchmark::ReferenceStrategy::ON;
  } else if (ref_strategy_str == kRefsOffFlag) {
    ref_strategy = test::benchmark::PutBenchmark::ReferenceStrategy::OFF;
  } else if (ref_strategy_str == kRefsAutoFlag) {
    ref_strategy = test::benchmark::PutBenchmark::ReferenceStrategy::AUTO;
  } else {
    std::cerr << "Unknown option " << ref_strategy_str << " for "
              << kRefsFlag.ToString() << std::endl;
    PrintUsage(argv[0]);
    return -1;
  }

  int seed;
  std::string seed_str;
  if (command_line.GetOptionValue(kSeedFlag.ToString(), &seed_str)) {
    if (!fxl::StringToNumberWithError(seed_str, &seed)) {
      PrintUsage(argv[0]);
      return -1;
    }
  } else {
    seed = fxl::RandUint64();
  }

  mtl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());
  test::benchmark::PutBenchmark app(entry_count, transaction_size, key_size,
                                    value_size, update, ref_strategy, seed);
  // TODO(nellyv): A delayed task is necessary because of US-257.
  loop.task_runner()->PostDelayedTask([&app] { app.Run(); },
                                      fxl::TimeDelta::FromSeconds(1));
  loop.Run();
  return 0;
}
