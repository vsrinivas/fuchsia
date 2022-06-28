// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/agis/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/in.h>
#include <zircon/system/ulib/fbl/include/fbl/unique_fd.h>

#include <unordered_map>
#include <unordered_set>

namespace {
// Value struct for |registry| below.
struct RegistryValue {
  RegistryValue(zx_koid_t process_koid_in, std::string process_name_in, zx::socket agi_socket_in)
      : process_koid(process_koid_in),
        process_name(std::move(process_name_in)),
        agi_socket(agi_socket_in.release()) {}
  zx_koid_t process_koid;
  std::string process_name;
  zx::socket agi_socket;
};

// Map ids to |RegistryValue|s.
std::unordered_map<uint64_t, RegistryValue> registry;
}  // namespace

class ComponentRegistryImpl final : public fuchsia::gpu::agis::ComponentRegistry {
 public:
  ~ComponentRegistryImpl() override {
    for (const auto &key : keys_) {
      registry.erase(key);
    }
  }

  // Add entries to the |registry| map.
  void Register(uint64_t id, zx_koid_t process_koid, std::string process_name,
                RegisterCallback callback) override {
    fuchsia::gpu::agis::ComponentRegistry_Register_Result result;

    auto matched_iter = registry.find(id);
    if (matched_iter != registry.end()) {
      result.set_err(fuchsia::gpu::agis::Error::ALREADY_REGISTERED);
      callback(std::move(result));
      return;
    }

    // Test if the vtc map is full.
    if (registry.size() == fuchsia::gpu::agis::MAX_VTCS) {
      result.set_err(fuchsia::gpu::agis::Error::VTCS_EXCEEDED);
      callback(std::move(result));
      return;
    }

    zx::socket gapii_layer_socket, agi_socket;
    auto status = zx::socket::create(0u, &gapii_layer_socket, &agi_socket);
    if (status != ZX_OK) {
      FX_SLOG(ERROR, "ComponentRegistryImpl::Register zx::socket::create() failed",
              KV("status", status));
      result.set_err(fuchsia::gpu::agis::Error::INTERNAL_ERROR);
      callback(std::move(result));
    }

    keys_.insert(id);
    registry.insert(
        std::make_pair(id, RegistryValue(process_koid, process_name, std::move(agi_socket))));
    fuchsia::gpu::agis::ComponentRegistry_Register_Response response(std::move(gapii_layer_socket));
    result.set_response(std::move(response));
    callback(std::move(result));
  }

  void Unregister(uint64_t id, UnregisterCallback callback) override {
    fuchsia::gpu::agis::ComponentRegistry_Unregister_Result result;
    size_t num_erased = registry.erase(id);
    if (num_erased) {
      keys_.erase(id);
      result.set_response(fuchsia::gpu::agis::ComponentRegistry_Unregister_Response());
    } else {
      result.set_err(fuchsia::gpu::agis::Error::NOT_FOUND);
    }
    callback(std::move(result));
  }

  void AddBinding(std::unique_ptr<ComponentRegistryImpl> session,
                  fidl::InterfaceRequest<fuchsia::gpu::agis::ComponentRegistry> &&request) {
    bindings_.AddBinding(std::move(session), std::move(request));
  }

 private:
  fidl::BindingSet<fuchsia::gpu::agis::ComponentRegistry,
                   std::unique_ptr<fuchsia::gpu::agis::ComponentRegistry>>
      bindings_;
  std::unordered_set<uint64_t> keys_;
};

class ObserverImpl final : public fuchsia::gpu::agis::Observer {
 public:
  void Vtcs(VtcsCallback callback) override {
    fuchsia::gpu::agis::Observer_Vtcs_Result result;
    std::vector<fuchsia::gpu::agis::Vtc> vtcs;
    for (const auto &element : registry) {
      auto vtc = ::fuchsia::gpu::agis::Vtc::New();
      vtc->set_process_koid(element.second.process_koid);
      vtc->set_process_name(element.second.process_name);
      zx::socket agi_socket_clone;
      zx_status_t status =
          element.second.agi_socket.duplicate(ZX_RIGHT_SAME_RIGHTS, &agi_socket_clone);
      if (status != ZX_OK) {
        FX_SLOG(ERROR, "ObserverImpl::Vtcs socket duplicate failed", KV("status", status));
        result.set_err(fuchsia::gpu::agis::Error::INTERNAL_ERROR);
        callback(std::move(result));
      }
      vtc->set_agi_socket(std::move(agi_socket_clone));
      vtcs.emplace_back(std::move(*vtc));
    }
    fuchsia::gpu::agis::Observer_Vtcs_Response response(std::move(vtcs));
    result.set_response(std::move(response));
    callback(std::move(result));
  }

  void AddBinding(std::unique_ptr<ObserverImpl> observer,
                  fidl::InterfaceRequest<fuchsia::gpu::agis::Observer> &&request) {
    bindings_.AddBinding(std::move(observer), std::move(request));
  }

 private:
  fidl::BindingSet<fuchsia::gpu::agis::Observer, std::unique_ptr<fuchsia::gpu::agis::Observer>>
      bindings_;
};

int main(int argc, const char **argv) {
  syslog::SetTags({"agis"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(
      fidl::InterfaceRequestHandler<fuchsia::gpu::agis::ComponentRegistry>(
          [](fidl::InterfaceRequest<fuchsia::gpu::agis::ComponentRegistry> request) {
            auto component_registry = std::make_unique<ComponentRegistryImpl>();
            component_registry->AddBinding(std::move(component_registry), std::move(request));
          }));
  context->outgoing()->AddPublicService(fidl::InterfaceRequestHandler<fuchsia::gpu::agis::Observer>(
      [](fidl::InterfaceRequest<fuchsia::gpu::agis::Observer> request) {
        auto observer = std::make_unique<ObserverImpl>();
        observer->AddBinding(std::move(observer), std::move(request));
      }));

  return loop.Run();
}
