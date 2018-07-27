// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gtest/gtest.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/tests/e2e_sync/ledger_app_instance_factory_e2e.h"

namespace test {
namespace {
constexpr fxl::StringView kServerIdFlag = "server-id";
std::string* server_id_ptr = nullptr;

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kServerIdFlag
            << "=<string>" << std::endl;
}
}  // namespace

std::vector<LedgerAppInstanceFactory*> GetLedgerAppInstanceFactories() {
  static std::unique_ptr<LedgerAppInstanceFactory> factory;
  static std::once_flag flag;

  auto factory_ptr = &factory;
  std::call_once(flag, [factory_ptr] {
    auto factory =
        std::make_unique<LedgerAppInstanceFactoryImpl>(*server_id_ptr);
    factory->Init();
    *factory_ptr = std::move(factory);
  });

  return {factory.get()};
}

}  // namespace test

int main(int argc, char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string server_id;
  if (!command_line.GetOptionValue(test::kServerIdFlag.ToString(),
                                   &server_id)) {
    test::PrintUsage(argv[0]);
    return -1;
  }
  test::server_id_ptr = new std::string(server_id);

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
