// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/agis/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/global.h>
#include <netinet/in.h>
#include <zircon/system/ulib/fbl/include/fbl/unique_fd.h>

#include <unordered_map>
#include <unordered_set>

#include <sdk/lib/fdio/include/lib/fdio/fd.h>

namespace {
// Value struct for |registry| below.
struct RegistryValue {
  RegistryValue(std::string process_name_in, uint16_t port_in)
      : process_name(std::move(process_name_in)), port(port_in) {}
  std::string process_name;
  uint16_t port;
};

// Map process IDs to |RegistryValue|s.
std::unordered_map<zx_koid_t, RegistryValue> registry;
}  // namespace

class SessionImpl final : public fuchsia::gpu::agis::Session {
 public:
  ~SessionImpl() override {
    for (const auto &key : keys_) {
      registry.erase(key);
    }
  }

  // Add entries to the |registry| map.
  void Register(zx_koid_t process_id, std::string process_name,
                RegisterCallback callback) override {
    fuchsia::gpu::agis::Session_Register_Result result;

    auto matched_iter = registry.find(process_id);
    if (matched_iter != registry.end()) {
      result.set_err(fuchsia::gpu::agis::Status::ALREADY_REGISTERED);
      callback(std::move(result));
      return;
    }

    // Test if the connection map is full.
    if (registry.size() == fuchsia::gpu::agis::MAX_CONNECTIONS) {
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
    keys_.insert(process_id);
    registry.insert(std::make_pair(process_id, RegistryValue(process_name, port)));
    fuchsia::gpu::agis::Session_Register_Response response(std::move(socket_handle));
    result.set_response(std::move(response));
    callback(std::move(result));
  }

  void Unregister(zx_koid_t process_id, UnregisterCallback callback) override {
    fuchsia::gpu::agis::Session_Unregister_Result result;
    size_t num_erased = registry.erase(process_id);
    if (num_erased) {
      keys_.erase(process_id);
      result.set_response(fuchsia::gpu::agis::Session_Unregister_Response());
    } else {
      result.set_err(fuchsia::gpu::agis::Status::NOT_FOUND);
    }
    callback(std::move(result));
  }

  void Connections(ConnectionsCallback callback) override {
    fuchsia::gpu::agis::Session_Connections_Result result;
    std::vector<fuchsia::gpu::agis::Connection> connections;
    for (const auto &element : registry) {
      auto connection = ::fuchsia::gpu::agis::Connection::New();
      connection->set_process_id(element.first);
      connection->set_process_name(element.second.process_name);
      connection->set_port(element.second.port);
      connections.emplace_back(std::move(*connection));
    }
    fuchsia::gpu::agis::Session_Connections_Response response(std::move(connections));
    result.set_response(std::move(response));
    callback(std::move(result));
  }

  void AddBinding(std::unique_ptr<SessionImpl> session,
                  fidl::InterfaceRequest<fuchsia::gpu::agis::Session> &&request) {
    bindings_.AddBinding(std::move(session), std::move(request));
  }

 private:
  fidl::BindingSet<fuchsia::gpu::agis::Session, std::unique_ptr<fuchsia::gpu::agis::Session>>
      bindings_;
  std::unordered_set<zx_koid_t> keys_;
};

int main(int argc, const char **argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(fidl::InterfaceRequestHandler<fuchsia::gpu::agis::Session>(
      [](fidl::InterfaceRequest<fuchsia::gpu::agis::Session> request) {
        auto session = std::make_unique<SessionImpl>();
        session->AddBinding(std::move(session), std::move(request));
      }));

  return loop.Run();
}
