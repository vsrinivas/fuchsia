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

BaseIntegrationTest::BaseIntegrationTest()
    : loop_(&kAsyncLoopConfigNoAttachToThread) {}

BaseIntegrationTest::~BaseIntegrationTest() = default;

void BaseIntegrationTest::RunLoop() { RealLoopFixture::RunLoop(); }

void BaseIntegrationTest::StopLoop() { QuitLoop(); }

void BaseIntegrationTest::SetUp() {
  ::testing::Test::SetUp();
  trace_provider_ = std::make_unique<trace::TraceProvider>(dispatcher());
  loop_.StartThread();
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

}  // namespace integration
}  // namespace test
