// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

// [START fidl_includes]
#include <fidl/fuchsia.examples/cpp/fidl.h>
// [END fidl_includes]

// [START includes]
#include <lib/async-loop/cpp/loop.h>
#include <lib/svc/outgoing.h>
// [END includes]

// [START server]
class EchoImpl : public fidl::Server<fuchsia_examples::Echo> {
 public:
  // [START bind_server]
  // Bind a new implementation to a channel. The implementation deletes itself
  // when the connection tears down.
  static void BindSelfManagedServer(async_dispatcher_t* dispatcher,
                                    fidl::ServerEnd<fuchsia_examples::Echo> request) {
    std::unique_ptr impl = std::make_unique<EchoImpl>();
    EchoImpl* impl_ptr = impl.get();
    fidl::ServerBindingRef binding_ref = fidl::BindServer(
        dispatcher, std::move(request), std::move(impl), std::mem_fn(&EchoImpl::OnUnbound));
    impl_ptr->binding_ref_.emplace(std::move(binding_ref));
  }
  // [END bind_server]

  // Called when the connection is torn down, shortly before the implementation is destroyed.
  void OnUnbound(fidl::UnbindInfo info, fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
    if (info.is_peer_closed()) {
      std::cout << "Client disconnected" << std::endl;
    } else if (!info.is_user_initiated()) {
      std::cerr << "server error: " << info << std::endl;
    }
  }

  void EchoString(EchoStringRequest& request, EchoStringCompleter::Sync& completer) override {
    completer.Reply({request.value()});
  }

  void SendString(SendStringRequest& request, SendStringCompleter::Sync& completer) override {
    if (!binding_ref_.has_value()) {
      return;
    }

    fitx::result result = fidl::SendEvent(*binding_ref_)->OnString({request.value()});
    if (!result.is_ok()) {
      std::cerr << "Error sending event: " << result.error_value() << std::endl;
    }
  }

 private:
  std::optional<fidl::ServerBindingRef<fuchsia_examples::Echo>> binding_ref_;
};
// [END server]

// [START main]
int main(int argc, const char** argv) {
  // Initialize the async loop. The Echo server will use the dispatcher of this
  // loop to listen for incoming requests.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create an Outgoing class which will serve requests from the /svc/ directory.
  svc::Outgoing outgoing(loop.dispatcher());
  zx_status_t status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    std::cerr << "error: ServeFromStartupInfo returned: " << status << " ("
              << zx_status_get_string(status) << ")" << std::endl;
    return -1;
  }

  // Register a handler for components trying to connect to fuchsia.examples.Echo.
  status = outgoing.svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_examples::Echo>,
      fbl::MakeRefCounted<fs::Service>(
          [dispatcher](fidl::ServerEnd<fuchsia_examples::Echo> request) mutable {
            std::cout << "Incoming connection for "
                      << fidl::DiscoverableProtocolName<fuchsia_examples::Echo> << std::endl;
            EchoImpl::BindSelfManagedServer(dispatcher, std::move(request));
            return ZX_OK;
          }));

  std::cout << "Running unified C++ echo server" << std::endl;
  loop.Run();
  return 0;
}
// [END main]
