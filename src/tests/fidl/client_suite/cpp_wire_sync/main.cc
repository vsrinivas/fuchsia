// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.clientsuite/cpp/wire_messaging.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>

#include "src/tests/fidl/client_suite/cpp_util/error_util.h"

class RunnerServer : public fidl::WireServer<fidl_clientsuite::Runner> {
 public:
  RunnerServer() {}

  void IsTestEnabled(IsTestEnabledRequestView request,
                     IsTestEnabledCompleter::Sync& completer) override {
    completer.Reply(true);
  }

  void CheckAlive(CheckAliveCompleter::Sync& completer) override { completer.Reply(); }

  void CallTwoWayNoPayload(CallTwoWayNoPayloadRequestView request,
                           CallTwoWayNoPayloadCompleter::Sync& completer) override {
    auto client = fidl::WireSyncClient(std::move(request->target));
    auto result = client->TwoWayNoPayload();
    if (result.ok()) {
      completer.Reply(
          fidl::WireResponse<fidl_clientsuite::Runner::CallTwoWayNoPayload>::WithSuccess(
              ::fidl_clientsuite::wire::Empty()));
    } else {
      completer.Reply(
          fidl::WireResponse<fidl_clientsuite::Runner::CallTwoWayNoPayload>::WithFidlError(
              clienttest_util::ClassifyError(result)));
    }
  }
};

int main(int argc, const char** argv) {
  std::cout << "CPP wire sync client: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  ZX_ASSERT(outgoing.ServeFromStartupInfo().is_ok());
  RunnerServer runner_server;
  auto result = outgoing.AddProtocol<fidl_clientsuite::Runner>(&runner_server);
  ZX_ASSERT(result.is_ok());

  std::cout << "CPP wire sync client: ready!" << std::endl;
  return loop.Run();
}
