// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/sync_tests/lib.h"

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/test/app_test.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/directory.h"

namespace {
constexpr ftl::StringView kServerIdFlag = "server-id";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kServerIdFlag
            << "=<string>" << std::endl;
}

}  // namespace

namespace sync_test {
namespace {
constexpr ftl::StringView kStoragePath = "/data/sync_test/ledger/sync";

std::string* server_id;
}  // namespace

SyncTest::LedgerPtrHolder::LedgerPtrHolder(
    files::ScopedTempDir dir,
    app::ApplicationControllerPtr controller,
    ledger::LedgerPtr ledger)
    : ledger(std::move(ledger)),
      dir_(std::move(dir)),
      controller_(std::move(controller)) {}

SyncTest::LedgerPtrHolder::~LedgerPtrHolder() {}

SyncTest::SyncTest()
    : token_provider_impl_("",
                           "sync_user",
                           "sync_user@google.com",
                           "client_id") {}

SyncTest::~SyncTest() {}

void SyncTest::SetUp() {
  ::testing::Test::SetUp();
}

std::unique_ptr<SyncTest::LedgerPtrHolder> SyncTest::GetLedger(
    std::string ledger_name,
    test::Erase erase) {
  ledger::LedgerPtr ledger_ptr;
  app::ApplicationControllerPtr controller;
  files::ScopedTempDir dir(kStoragePath);

  ledger::Status status = test::GetLedger(
      &message_loop_, application_context(), &controller, &token_provider_impl_,
      ledger_name, dir.path(), test::SyncState::CLOUD_SYNC_ENABLED, *server_id,
      &ledger_ptr, erase);
  EXPECT_EQ(ledger::Status::OK, status);
  if (status != ledger::Status::OK) {
    FTL_LOG(ERROR) << "Unable to get a ledger.";
    return nullptr;
  }
  return std::make_unique<SyncTest::LedgerPtrHolder>(
      std::move(dir), std::move(controller), std::move(ledger_ptr));
}

}  // namespace sync_test

int main(int argc, char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  // TODO(etiennej): Refactor this code to use a different run loop for each
  // test. See LE-268.
  std::string server_id;
  if (!command_line.GetOptionValue(kServerIdFlag.ToString(), &server_id)) {
    PrintUsage(argv[0]);
    return -1;
  }
  sync_test::server_id = &server_id;

  return test::TestMain(argc, argv);
}
