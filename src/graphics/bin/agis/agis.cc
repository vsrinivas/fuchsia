// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/agis/cpp/fidl.h>
#include <fuchsia/url/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/global.h>
#include <netinet/in.h>
#include <zircon/system/ulib/fbl/include/fbl/unique_fd.h>

#include <unordered_map>

#include <sdk/lib/fdio/include/lib/fdio/fd.h>
#include <zx/cpp/fidl.h>

namespace {}

class SessionImpl : public fuchsia::gpu::agis::Session {
 public:
  // Add entries to the url_to_socket_ map.
  void Register(std::string component_url, RegisterCallback callback) override {
    fuchsia::gpu::agis::Session_Register_Result result;

    auto matched_iter = url_to_port_.find(component_url);
    if (matched_iter != url_to_port_.end()) {
      result.set_err(fuchsia::gpu::agis::Status::ALREADY_REGISTERED);
      callback(std::move(result));
      return;
    }

    // Test if the connection map is full.
    if (url_to_port_.size() == fuchsia::gpu::agis::MAX_CONNECTIONS) {
      result.set_err(fuchsia::gpu::agis::Status::CONNECTIONS_EXCEEDED);
      callback(std::move(result));
      return;
    }

    // Create a socket and find a port to bind it to.
    fbl::unique_fd server_fd(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!server_fd.is_valid()) {
      result.set_err(fuchsia::gpu::agis::Status::INTERNAL_ERROR);
      FX_LOGF(ERROR, "agis", "SessionImpl::Register socket() failed with %s", strerror(errno));
      callback(std::move(result));
      return;
    }
    struct in_addr in_addr {
      .s_addr = INADDR_ANY
    };
    struct sockaddr_in server_addr {
      .sin_family = AF_INET, .sin_port = 0, .sin_addr = in_addr
    };
    const int bind_rv = bind(server_fd.get(), reinterpret_cast<struct sockaddr *>(&server_addr),
                             sizeof(server_addr));
    if (bind_rv < 0) {
      result.set_err(fuchsia::gpu::agis::Status::INTERNAL_ERROR);
      FX_LOGF(ERROR, "agis", "SessionImpl::Register BIND FAILED with %s", strerror(errno));
      callback(std::move(result));
      return;
    }

    struct sockaddr bound_addr = {};
    socklen_t address_len = sizeof(struct sockaddr);
    getsockname(server_fd.get(), &bound_addr, &address_len);
    assert(address_len == sizeof(sockaddr_in));
    struct sockaddr_in *bound_addr_in = reinterpret_cast<struct sockaddr_in *>(&bound_addr);
    const uint16_t port = ntohs(bound_addr_in->sin_port);
    if (port == 0) {
      FX_LOG(ERROR, "agis", "SessionImpl::Register BAD PORT");
      result.set_err(fuchsia::gpu::agis::Status::INTERNAL_ERROR);
      callback(std::move(result));
      return;
    }

    // Remove |server_fd| from the descriptor table of this process and convert |server_fd| to a
    // FIDL-transferrable handle.
    zx::handle socket_handle;
    if (fdio_fd_transfer_or_clone(server_fd.get(), socket_handle.reset_and_get_address()) !=
        ZX_OK) {
      result.set_err(fuchsia::gpu::agis::Status::INTERNAL_ERROR);
      FX_LOG(ERROR, "agis", "SessionImpl::Register socket fd to handle transfer failed");
      callback(std::move(result));
      return;
    }

    server_fd.release();
    url_to_port_.insert(std::make_pair(component_url, port));
    fuchsia::gpu::agis::Session_Register_Response response(std::move(socket_handle));
    result.set_response(std::move(response));

    callback(std::move(result));
  }

  void Unregister(std::string component_url, UnregisterCallback callback) override {
    fuchsia::gpu::agis::Session_Unregister_Result result;
    const auto &element_iter = url_to_port_.find(component_url);
    if (element_iter != url_to_port_.end()) {
      url_to_port_.erase(element_iter);
      result.set_response(fuchsia::gpu::agis::Session_Unregister_Response());
    } else {
      result.set_err(fuchsia::gpu::agis::Status::NOT_FOUND);
    }
    callback(std::move(result));
  }

  void Connections(ConnectionsCallback callback) override {
    fuchsia::gpu::agis::Session_Connections_Result result;
    std::vector<fuchsia::gpu::agis::Connection> connections;
    for (const auto &element : url_to_port_) {
      auto connection = ::fuchsia::gpu::agis::Connection::New();
      connection->component_url = element.first;
      connection->port = element.second;
      connections.emplace_back(*connection);
    }
    fuchsia::gpu::agis::Session_Connections_Response response(std::move(connections));
    result.set_response(std::move(response));
    callback(std::move(result));
  }

 private:
  std::unordered_map<std::string, uint16_t> url_to_port_;
};

int main(int argc, const char **argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  SessionImpl impl;
  fidl::BindingSet<fuchsia::gpu::agis::Session> bindings;
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(bindings.GetHandler(&impl));

  return loop.Run();
}
