// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>
#include <type_traits>

#include "src/tests/fidl/client_suite/harness/harness.h"

class HarnessServer : public fidl::Server<fidl_clientsuite::Harness> {
 public:
  HarnessServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void Start(StartRequest& request, StartCompleter::Sync& completer) override {
    auto finisher_endpoints = fidl::CreateEndpoints<fidl_clientsuite::Finisher>();
    auto finisher = std::make_shared<client_suite::internal::Finisher>();
    fidl::BindServer(dispatcher_, std::move(finisher_endpoints->server), finisher,
                     [](auto, fidl::UnbindInfo info, auto) {
                       ZX_ASSERT_MSG(info.is_dispatcher_shutdown() || info.is_user_initiated() ||
                                         info.is_peer_closed(),
                                     "finisher unbound with error");
                     });

    auto target_endpoints = fidl::CreateEndpoints<fidl_clientsuite::Target>();
    channel_util::Channel server_channel(target_endpoints->server.TakeChannel());

    auto test_handler = client_suite::internal::LookupTestHandler(request.test());
    completer.Reply({{std::move(target_endpoints->client), std::move(finisher_endpoints->client)}});
    test_handler(std::move(server_channel), std::move(finisher));
  }

 private:
  async_dispatcher_t* dispatcher_;
};

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  ZX_ASSERT(outgoing.ServeFromStartupInfo().is_ok());
  HarnessServer harness_server(loop.dispatcher());
  auto result = outgoing.AddProtocol<fidl_clientsuite::Harness>(&harness_server);
  ZX_ASSERT(result.is_ok());

  FX_LOGS(INFO) << "Test harness: ready!";
  return loop.Run();
}
