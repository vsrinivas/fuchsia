// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/integration/sync/lib.h"

#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/directory.h"

namespace test {
namespace integration {
namespace sync {
namespace {
constexpr fxl::StringView kServerIdFlag = "server-id";
std::string* server_id = nullptr;

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kServerIdFlag
            << "=<string>" << std::endl;
}

}  // namespace

SyncTest::SyncTest() {}

SyncTest::~SyncTest() {}

void SyncTest::SetUp() {
  ::testing::Test::SetUp();
  app_factory_ = GetLedgerAppInstanceFactory();
  if (server_id) {
    app_factory_->SetServerId(*server_id);
  }
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
SyncTest::NewLedgerAppInstance() {
  return app_factory_->NewLedgerAppInstance();
}

void ProcessCommandLine(int argc, char** argv) {
  FXL_DCHECK(!test::integration::sync::server_id);

  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  // TODO(etiennej): Refactor this code to use a different run loop for each
  // test. See LE-268.
  std::string server_id;
  if (!command_line.GetOptionValue(kServerIdFlag.ToString(), &server_id)) {
    PrintUsage(argv[0]);
    return;
  }
  test::integration::sync::server_id = new std::string(server_id);
}
}  // namespace sync
}  // namespace integration
}  // namespace test
