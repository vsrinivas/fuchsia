// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/handlers.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>

// An implementation of the Echo protocol. Protocols are implemented in LLCPP by
// creating a subclass of the ::Interface class for the protocol.
class EchoImpl final : public fidl::WireServer<fuchsia_examples::Echo> {
 public:
  // Bind this implementation to a channel.
  EchoImpl(bool reverse, async_dispatcher_t* dispatcher,
           fidl::ServerEnd<fuchsia_examples::Echo> request)
      : reverse_(reverse),
        binding_(fidl::BindServer(dispatcher, std::move(request), this,
                                  // This is a fidl::OnUnboundFn<EchoImpl>.
                                  [this](EchoImpl* impl, fidl::UnbindInfo info,
                                         fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
                                    if (info.is_peer_closed()) {
                                      std::cout << "Client disconnected" << std::endl;
                                    } else if (!info.is_user_initiated()) {
                                      std::cerr << "server error: " << info << std::endl;
                                    }
                                    delete this;
                                  })) {}

  // Handle a SendString request by sending on OnString event with the request value. For
  // fire and forget methods, the completer can be used to close the channel with an epitaph.
  void SendString(SendStringRequestView request, SendStringCompleter::Sync& completer) override {}

  // Handle an EchoString request by responding with the request value. For two-way
  // methods, the completer is also used to send a response.
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    std::string value(request->value.get());
    std::cout << "Got echo request: " << value << std::endl;
    if (reverse_) {
      std::reverse(value.begin(), value.end());
    }
    std::cout << "Sending response: " << value << std::endl;
    auto reply = fidl::StringView::FromExternal(value);
    completer.Reply(reply);
  }

 private:
  const bool reverse_;
  fidl::ServerBindingRef<fuchsia_examples::Echo> binding_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());

  component::ServiceInstanceHandler handler;
  fuchsia_examples::EchoService::Handler my_service(&handler);

  // Example of serving members of a service instance.
  auto add_regular_result = my_service.add_regular_echo(
      [&loop](fidl::ServerEnd<fuchsia_examples::Echo> request_channel) -> void {
        new EchoImpl(false, loop.dispatcher(), std::move(request_channel));
      });
  ZX_ASSERT(add_regular_result.is_ok());

  auto add_reversed_result = my_service.add_reversed_echo(
      [&loop](fidl::ServerEnd<fuchsia_examples::Echo> request_channel) -> void {
        new EchoImpl(true, loop.dispatcher(), std::move(request_channel));
      });
  ZX_ASSERT(add_reversed_result.is_ok());

  // Example of serving an instance of "EchoService".
  auto result = outgoing.AddService<fuchsia_examples::EchoService>(std::move(handler));
  if (result.is_error()) {
    return result.status_value();
  }
  result = outgoing.ServeFromStartupInfo();
  if (result.is_error()) {
    return result.status_value();
  }

  std::cout << "Running echo server" << std::endl;
  loop.Run();
  return 0;
}
