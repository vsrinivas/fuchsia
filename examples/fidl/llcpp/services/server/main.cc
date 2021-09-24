// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/service/llcpp/outgoing_directory.h>
#include <lib/service/llcpp/service.h>
#include <lib/service/llcpp/service_handler.h>

#include <iostream>

// An implementation of the Echo protocol. Protocols are implemented in LLCPP by
// creating a subclass of the ::Interface class for the protocol.
class EchoImpl final : public fidl::WireServer<fuchsia_examples::Echo> {
 public:
  explicit EchoImpl(bool reverse) : reverse_(reverse) {}

  // Bind this implementation to a channel.
  void Bind(std::unique_ptr<EchoImpl> self, async_dispatcher_t* dispatcher,
            fidl::ServerEnd<fuchsia_examples::Echo> request) {
    // Destroy |self| when the unbound handler completes.
    fidl::OnUnboundFn<EchoImpl> unbound_handler =
        [self = std::move(self)](EchoImpl* impl, fidl::UnbindInfo info,
                                 fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
          switch (info.reason()) {
            case fidl::Reason::kClose:
            case fidl::Reason::kUnbind:
              // These are initiated by ourself.
              break;
            case fidl::Reason::kPeerClosed:
              std::cout << "Client disconnected" << std::endl;
              break;
            default:
              std::cerr << "Server error: " << info << std::endl;
          }
        };
    fidl::BindServer(dispatcher, std::move(request), this, std::move(unbound_handler));
  }

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
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  llcpp::sys::OutgoingDirectory outgoing(loop.dispatcher());

  llcpp::sys::ServiceHandler handler;
  fuchsia_examples::EchoService::Handler my_service(&handler);

  // Example of serving members of a service instance.
  auto add_regular_result = my_service.add_regular_echo(
      [&loop](fidl::ServerEnd<fuchsia_examples::Echo> request_channel) -> zx::status<> {
        std::unique_ptr regular_impl = std::make_unique<EchoImpl>(false);
        regular_impl->Bind(std::move(regular_impl), loop.dispatcher(), std::move(request_channel));
        return zx::ok();
      });
  ZX_ASSERT(add_regular_result.is_ok());

  auto add_reversed_result = my_service.add_reversed_echo(
      [&loop](fidl::ServerEnd<fuchsia_examples::Echo> request_channel) -> zx::status<> {
        std::unique_ptr reversed_impl = std::make_unique<EchoImpl>(true);
        reversed_impl->Bind(std::move(reversed_impl), loop.dispatcher(),
                            std::move(request_channel));
        return zx::ok();
      });
  ZX_ASSERT(add_reversed_result.is_ok());

  // Example of serving an instance of "EchoService".
  outgoing.AddService<fuchsia_examples::EchoService>(std::move(handler));
  outgoing.ServeFromStartupInfo();

  std::cout << "Running echo server" << std::endl;
  loop.Run();
  return 0;
}
