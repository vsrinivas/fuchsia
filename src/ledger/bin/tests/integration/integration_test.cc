// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/integration/integration_test.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/ledger/lib/socket/socket_pair.h"
#include "src/ledger/lib/socket/socket_writer.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/socket/strings.h"

namespace ledger {

BaseIntegrationTest::BaseIntegrationTest(const LedgerAppInstanceFactoryBuilder* factory_builder)
    : factory_builder_(factory_builder) {}

BaseIntegrationTest::~BaseIntegrationTest() = default;

void BaseIntegrationTest::SetUp() {
  ::testing::Test::SetUp();
  factory_ = factory_builder_->NewFactory();
  trace_provider_ = std::make_unique<trace::TraceProviderWithFdio>(dispatcher());
  services_loop_ = GetLoopController()->StartNewLoop();
}

void BaseIntegrationTest::TearDown() {
  services_loop_.reset();
  ::testing::Test::TearDown();
}

void BaseIntegrationTest::RunLoop() { GetLoopController()->RunLoop(); }

void BaseIntegrationTest::StopLoop() { GetLoopController()->StopLoop(); }

std::unique_ptr<SubLoop> BaseIntegrationTest::StartNewLoop() {
  return GetLoopController()->StartNewLoop();
}

std::unique_ptr<CallbackWaiter> BaseIntegrationTest::NewWaiter() {
  return GetLoopController()->NewWaiter();
}

async_dispatcher_t* BaseIntegrationTest::dispatcher() { return GetLoopController()->dispatcher(); }

bool BaseIntegrationTest::RunLoopUntil(fit::function<bool()> condition) {
  return GetLoopController()->RunLoopUntil(std::move(condition));
}

void BaseIntegrationTest::RunLoopFor(zx::duration duration) {
  GetLoopController()->RunLoopFor(duration);
}

zx::socket BaseIntegrationTest::StreamDataToSocket(std::string data) {
  socket::SocketPair sockets;
  async::PostTask(services_loop_->dispatcher(),
                  [socket = std::move(sockets.socket1), data = std::move(data)]() mutable {
                    auto writer = new socket::StringSocketWriter();
                    writer->Start(std::move(data), std::move(socket));
                  });
  return std::move(sockets.socket2);
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
BaseIntegrationTest::NewLedgerAppInstance() {
  return GetAppFactory()->NewLedgerAppInstance();
}

LedgerAppInstanceFactory* BaseIntegrationTest::GetAppFactory() {
  FXL_DCHECK(factory_) << "|SetUp| has not been called.";
  return factory_.get();
}

LoopController* BaseIntegrationTest::GetLoopController() {
  return GetAppFactory()->GetLoopController();
}

rng::Random* BaseIntegrationTest::GetRandom() { return GetAppFactory()->GetRandom(); }

IntegrationTest::IntegrationTest() : BaseIntegrationTest(GetParam()) {}

IntegrationTest::~IntegrationTest() = default;

}  // namespace ledger
