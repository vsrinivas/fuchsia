// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/integration/integration_test.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/scoped_temp_dir.h>

#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/socket/socket_pair.h"
#include "peridot/lib/socket/socket_writer.h"

namespace test {
namespace integration {

namespace {
constexpr fxl::StringView kServerIdFlag = "server-id";
std::string* server_id = nullptr;

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kServerIdFlag
            << "=<string>" << std::endl;
}
}  // namespace

BaseIntegrationTest::BaseIntegrationTest() = default;

BaseIntegrationTest::~BaseIntegrationTest() = default;

void BaseIntegrationTest::RunLoop() { RealLoopFixture::RunLoop(); }

void BaseIntegrationTest::StopLoop() { QuitLoop(); }

void BaseIntegrationTest::SetUp() {
  ::testing::Test::SetUp();
  trace_provider_ = std::make_unique<trace::TraceProvider>(dispatcher());
  loop_.StartThread();
  if (server_id) {
    GetAppFactory()->SetServerId(*server_id);
  }
}

void BaseIntegrationTest::TearDown() {
  loop_.Shutdown();
  ::testing::Test::TearDown();
}

zx::socket BaseIntegrationTest::StreamDataToSocket(std::string data) {
  socket::SocketPair sockets;
  async::PostTask(loop_.dispatcher(), [socket = std::move(sockets.socket1),
                                  data = std::move(data)]() mutable {
    auto writer = new socket::StringSocketWriter();
    writer->Start(std::move(data), std::move(socket));
  });
  return std::move(sockets.socket2);
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
BaseIntegrationTest::NewLedgerAppInstance() {
  return GetAppFactory()->NewLedgerAppInstance(this);
}

IntegrationTest::IntegrationTest() = default;

IntegrationTest::~IntegrationTest() = default;

LedgerAppInstanceFactory* IntegrationTest::GetAppFactory() {
  return GetParam();
}

bool ProcessCommandLine(int argc, char** argv) {
  FXL_DCHECK(!test::integration::server_id);

  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string server_id;
  if (!command_line.GetOptionValue(kServerIdFlag.ToString(), &server_id)) {
    PrintUsage(argv[0]);
    return false;
  }
  test::integration::server_id = new std::string(server_id);
  return true;
}

}  // namespace integration
}  // namespace test
