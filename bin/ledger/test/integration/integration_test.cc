// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/integration/integration_test.h"

#include <thread>

#include "peridot/bin/ledger/app/erase_remote_repository_operation.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/callback/synchronous_task.h"
#include "peridot/bin/ledger/glue/socket/socket_pair.h"
#include "peridot/bin/ledger/glue/socket/socket_writer.h"
#include "peridot/bin/ledger/network/network_service_impl.h"
#include "peridot/bin/ledger/test/fake_token_provider.h"
#include "peridot/bin/ledger/test/integration/test_utils.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/functional/make_copyable.h"

namespace test {
namespace integration {
void IntegrationTest::SetUp() {
  ::testing::Test::SetUp();
  socket_thread_ = fsl::CreateThread(&socket_task_runner_);
  app_factory_ = GetLedgerAppInstanceFactory();
}

void IntegrationTest::TearDown() {
  socket_task_runner_->PostTask(
      [] { fsl::MessageLoop::GetCurrent()->QuitNow(); });
  socket_thread_.join();

  ::testing::Test::TearDown();
}

zx::socket IntegrationTest::StreamDataToSocket(std::string data) {
  glue::SocketPair sockets;
  socket_task_runner_->PostTask(fxl::MakeCopyable([
    socket = std::move(sockets.socket1), data = std::move(data)
  ]() mutable {
    auto writer = new glue::StringSocketWriter();
    writer->Start(std::move(data), std::move(socket));
  }));
  return std::move(sockets.socket2);
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
IntegrationTest::NewLedgerAppInstance() {
  return app_factory_->NewLedgerAppInstance();
}

}  // namespace integration
}  // namespace test
