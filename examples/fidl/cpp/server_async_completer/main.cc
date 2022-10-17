// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for the C++ async response tutorial.
// Head over there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/async-completer
// ============================================================================

#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>

class EchoImpl : public fidl::Server<fuchsia_examples::Echo> {
 public:
  explicit EchoImpl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // [START impl-echo-string]
  void EchoString(EchoStringRequest& request, EchoStringCompleter::Sync& completer) override {
    async::PostDelayedTask(
        dispatcher_,
        [value = request.value(), completer = completer.ToAsync()]() mutable {
          completer.Reply({{.response = value}});
        },
        zx::duration(ZX_SEC(1)));
  }
  // [END impl-echo-string]

  void SendString(SendStringRequest& request, SendStringCompleter::Sync& completer) override {
    ZX_ASSERT(binding_ref_.has_value());

    fit::result result = fidl::SendEvent(*binding_ref_)->OnString({request.value()});
    if (!result.is_ok()) {
      FX_LOGS(ERROR) << "Error sending event: " << result.error_value();
    }
  }

  static void BindSelfManagedServer(async_dispatcher_t* dispatcher,
                                    fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
    std::unique_ptr impl = std::make_unique<EchoImpl>(dispatcher);
    EchoImpl* impl_ptr = impl.get();
    fidl::ServerBindingRef binding_ref = fidl::BindServer(
        dispatcher, std::move(server_end), std::move(impl), std::mem_fn(&EchoImpl::OnUnbound));
    impl_ptr->binding_ref_.emplace(std::move(binding_ref));
  }

  void OnUnbound(fidl::UnbindInfo info, fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
    if (info.is_user_initiated()) {
      return;
    }
    if (info.is_peer_closed()) {
      FX_LOGS(INFO) << "Client disconnected";
    } else {
      FX_LOGS(ERROR) << "Server error: " << info;
    }
  }

 private:
  async_dispatcher_t* dispatcher_;
  std::optional<fidl::ServerBindingRef<fuchsia_examples::Echo>> binding_ref_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(dispatcher);
  zx::result result = outgoing.ServeFromStartupInfo();
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to serve outgoing directory: " << result.status_string();
    return -1;
  }
  result = outgoing.AddProtocol<fuchsia_examples::Echo>(
      [dispatcher](fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
        FX_LOGS(INFO) << "Incoming connection for "
                      << fidl::DiscoverableProtocolName<fuchsia_examples::Echo>;
        EchoImpl::BindSelfManagedServer(dispatcher, std::move(server_end));
      });
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to add Echo protocol: " << result.status_string();
    return -1;
  }

  FX_LOGS(INFO) << "Running C++ echo server with natural types";

  loop.Run();
  return 0;
}
