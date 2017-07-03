// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/sync_tests/lib.h"

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/test/get_ledger.h"
#include "apps/test_runner/lib/reporting/gtest_listener.h"
#include "apps/test_runner/lib/reporting/reporter.h"
#include "apps/test_runner/lib/reporting/results_queue.h"
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

app::ApplicationContext* context;
std::string* server_id;
}  // namespace

SyncTestBase::LedgerPtrHolder::LedgerPtrHolder(
    files::ScopedTempDir dir,
    app::ApplicationControllerPtr controller,
    ledger::LedgerPtr ledger)
    : ledger(std::move(ledger)),
      dir_(std::move(dir)),
      controller_(std::move(controller)) {}

SyncTestBase::LedgerPtrHolder::~LedgerPtrHolder() {}

SyncTestBase::SyncTestBase()
    : message_loop_(mtl::MessageLoop::GetCurrent()),
      token_provider_impl_("",
                           "sync_user",
                           "sync_user@google.com",
                           "client_id") {}

SyncTestBase::~SyncTestBase() {}

void SyncTestBase::SetUp() {
  ::testing::Test::SetUp();
}

std::unique_ptr<SyncTestBase::LedgerPtrHolder> SyncTestBase::GetLedger(
    std::string ledger_name,
    test::Erase erase) {
  ledger::LedgerPtr ledger_ptr;
  app::ApplicationControllerPtr controller;
  files::ScopedTempDir dir(kStoragePath);

  ledger::Status status = test::GetLedger(
      message_loop_, context, &controller, &token_provider_impl_, ledger_name,
      dir.path(), test::SyncState::CLOUD_SYNC_ENABLED, *server_id, &ledger_ptr,
      erase);
  EXPECT_EQ(ledger::Status::OK, status);
  return std::make_unique<SyncTestBase::LedgerPtrHolder>(
      std::move(dir), std::move(controller), std::move(ledger_ptr));
}

bool SyncTestBase::RunLoopWithTimeout(ftl::TimeDelta timeout) {
  return test::RunGivenLoopWithTimeout(message_loop_, timeout);
}

bool SyncTestBase::RunLoopUntil(std::function<bool()> condition,
                                ftl::TimeDelta timeout) {
  return test::RunGivenLoopUntil(message_loop_, std::move(condition), timeout);
}

ftl::Closure SyncTestBase::MakeQuitTask() {
  return [this] { message_loop_->PostQuitTask(); };
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

  mtl::MessageLoop loop;

  std::unique_ptr<app::ApplicationContext> application_context(
      app::ApplicationContext::CreateFromStartupInfo());
  sync_test::context = application_context.get();

  test_runner::ResultsQueue queue;
  test_runner::Reporter reporter(argv[0], &queue);
  test_runner::GTestListener listener(argv[0], &queue);
  reporter.Start(application_context.get());

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);
  return status;
}
