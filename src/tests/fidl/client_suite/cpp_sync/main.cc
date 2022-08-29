// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.clientsuite/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>

#include "src/tests/fidl/client_suite/cpp_util/error_util.h"

class RunnerServer : public fidl::Server<fidl_clientsuite::Runner> {
 public:
  RunnerServer() {}

  void IsTestEnabled(IsTestEnabledRequest& request,
                     IsTestEnabledCompleter::Sync& completer) override {
    completer.Reply(true);
  }

  void CheckAlive(CheckAliveRequest& request, CheckAliveCompleter::Sync& completer) override {
    completer.Reply();
  }

  void CallTwoWayNoPayload(CallTwoWayNoPayloadRequest& request,
                           CallTwoWayNoPayloadCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->TwoWayNoPayload();
    if (result.is_ok()) {
      completer.Reply(fidl::Response<fidl_clientsuite::Runner::CallTwoWayNoPayload>::WithSuccess(
          ::fidl_clientsuite::Empty()));
    } else {
      completer.Reply(fidl::Response<fidl_clientsuite::Runner::CallTwoWayNoPayload>::WithFidlError(
          clienttest_util::ClassifyError(result.error_value())));
    }
  }
};

int main(int argc, const char** argv) {
  std::cout << "CPP sync client: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  ZX_ASSERT(outgoing.ServeFromStartupInfo().is_ok());
  RunnerServer runner_server;
  auto result = outgoing.AddProtocol<fidl_clientsuite::Runner>(&runner_server);
  ZX_ASSERT(result.is_ok());

  std::cout << "CPP sync client: ready!" << std::endl;
  return loop.Run();
}
