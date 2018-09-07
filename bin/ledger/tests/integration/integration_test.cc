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

namespace ledger {

BaseIntegrationTest::BaseIntegrationTest() {}

BaseIntegrationTest::~BaseIntegrationTest() = default;

void BaseIntegrationTest::SetUp() {
  ::testing::Test::SetUp();
  trace_provider_ = std::make_unique<trace::TraceProvider>(dispatcher());
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

  async_dispatcher_t* BaseIntegrationTest::dispatcher() {
    return GetLoopController()->dispatcher();
  }

  fit::closure BaseIntegrationTest::QuitLoopClosure() {
    return GetLoopController()->QuitLoopClosure();
  }
  bool BaseIntegrationTest::RunLoopUntil(fit::function<bool()> condition) {
    return GetLoopController()->RunLoopUntil(std::move(condition));
  }

  bool BaseIntegrationTest::RunLoopFor(zx::duration duration) {
    return GetLoopController()->RunLoopFor(duration);
  }


zx::socket BaseIntegrationTest::StreamDataToSocket(std::string data) {
  socket::SocketPair sockets;
  async::PostTask(
      services_loop_->dispatcher(),
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

IntegrationTest::IntegrationTest() = default;

IntegrationTest::~IntegrationTest() = default;

void IntegrationTest::SetUp() {
  auto factory_builder = GetParam();
  factory_ = factory_builder->NewFactory();
  BaseIntegrationTest::SetUp();
}

LedgerAppInstanceFactory* IntegrationTest::GetAppFactory() {
  FXL_DCHECK(factory_) << "|SetUp| has not been called.";
  return factory_.get();
}

LoopController* IntegrationTest::GetLoopController() {
  FXL_DCHECK(factory_) << "|SetUp| has not been called.";
  return factory_->GetLoopController();
}

}  // namespace ledger
