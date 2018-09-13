// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gtest/gtest.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/testing/loop_controller_real_loop.h"
#include "peridot/bin/ledger/testing/sync_params.h"
#include "peridot/bin/ledger/tests/e2e_sync/ledger_app_instance_factory_e2e.h"

namespace ledger {
namespace {
ledger::SyncParams* sync_params_ptr = nullptr;

class FactoryBuilderE2eImpl : public LedgerAppInstanceFactoryBuilder {
 public:
  std::unique_ptr<LedgerAppInstanceFactory> NewFactory() const override {
    return std::make_unique<LedgerAppInstanceFactoryImpl>(
        std::make_unique<LoopControllerRealLoop>(), *sync_params_ptr);
  }
};

}  // namespace

std::vector<const LedgerAppInstanceFactoryBuilder*>
GetLedgerAppInstanceFactoryBuilders() {
  static auto static_builder = FactoryBuilderE2eImpl();
  std::vector<const LedgerAppInstanceFactoryBuilder*> builders = {
      &static_builder};
  return builders;
}

}  // namespace ledger

int main(int argc, char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  ledger::SyncParams sync_params;

  {
    async::Loop loop(&kAsyncLoopConfigAttachToThread);
    auto startup_context = component::StartupContext::CreateFromStartupInfo();

    if (!ledger::ParseSyncParamsFromCommandLine(
            command_line, startup_context.get(), &sync_params)) {
      std::cerr << ledger::GetSyncParamsUsage();
      return -1;
    }
    ledger::sync_params_ptr = &sync_params;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
