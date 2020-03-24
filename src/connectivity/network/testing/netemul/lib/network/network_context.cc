// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network_context.h"

#include <lib/async/default.h>

namespace netemul {

constexpr uint16_t kDefaultMtu = 1500;

class SetupHandle : fuchsia::netemul::network::SetupHandle {
 public:
  using FSetupHandle = fuchsia::netemul::network::SetupHandle;
  using OnClosedCallback = fit::function<void(SetupHandle*)>;

  SetupHandle(fidl::InterfaceRequest<FSetupHandle> req, async_dispatcher_t* dispatcher)
      : binding_(this, std::move(req), dispatcher) {
    binding_.set_error_handler([this](zx_status_t status) {
      channels_.clear();
      if (on_closed_callback_) {
        on_closed_callback_(this);
      }
    });
  }

  template <typename T>
  fidl::InterfaceRequest<T> CreateChannel() {
    fidl::InterfaceHandle<T> handle;
    auto ret = handle.NewRequest();
    AddChannel(handle.TakeChannel());
    return ret;
  }

  void AddChannel(zx::channel channel) { channels_.push_back(std::move(channel)); }

  void SetOnClosedCallback(OnClosedCallback callback) { on_closed_callback_ = std::move(callback); }

 private:
  fidl::Binding<FSetupHandle> binding_;
  std::vector<zx::channel> channels_;
  OnClosedCallback on_closed_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SetupHandle);
};

NetworkContext::NetworkContext(async_dispatcher_t* dispatcher)
    : network_manager_(this), endpoint_manager_(this) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }
  dispatcher_ = dispatcher;
}

NetworkContext::~NetworkContext() = default;

void NetworkContext::GetNetworkManager(
    ::fidl::InterfaceRequest<NetworkManager::FNetworkManager> net_manager) {
  network_manager_.Bind(std::move(net_manager));
}

void NetworkContext::GetEndpointManager(
    fidl::InterfaceRequest<EndpointManager::FEndpointManager> endp_manager) {
  endpoint_manager_.Bind(std::move(endp_manager));
}

zx_status_t NetworkContext::Setup(std::vector<NetworkSetup> setup,
                                  fidl::InterfaceRequest<FSetupHandle> req) {
  auto setup_handle = std::make_unique<SetupHandle>(std::move(req), dispatcher_);

  setup_handle->SetOnClosedCallback([this](SetupHandle* handle) {
    for (auto i = setup_handles_.begin(); i != setup_handles_.end(); i++) {
      if (i->get() == handle) {
        setup_handles_.erase(i);
        return;
      }
    }
  });

  for (auto& net_setup : setup) {
    auto status = network_manager_.CreateNetwork(net_setup.name, std::move(net_setup.config),
                                                 setup_handle->CreateChannel<Network::FNetwork>());
    if (status != ZX_OK) {
      return status;
    }

    auto* network = network_manager_.GetNetwork(net_setup.name);
    ZX_ASSERT(network != nullptr);

    for (auto& endp_setup : net_setup.endpoints) {
      if (!endp_setup.config) {
        // if not provided, use defaults:
        endp_setup.config = std::make_unique<Endpoint::Config>();
        endp_setup.config->mtu = kDefaultMtu;
        endp_setup.config->mac = nullptr;
        endp_setup.config->backing = fuchsia::netemul::network::EndpointBacking::ETHERTAP;
      }
      status = endpoint_manager_.CreateEndpoint(endp_setup.name, std::move(*endp_setup.config),
                                                endp_setup.link_up,
                                                setup_handle->CreateChannel<Endpoint::FEndpoint>());
      if (status != ZX_OK) {
        return status;
      }

      status = network->AttachEndpoint(std::move(endp_setup.name));
      if (status != ZX_OK) {
        return status;
      }
    }
  }
  setup_handles_.push_back(std::move(setup_handle));
  return ZX_OK;
}

void NetworkContext::Setup(std::vector<NetworkSetup> setup, SetupCallback callback) {
  fidl::InterfaceHandle<SetupHandle::FSetupHandle> ifhandle;
  auto status = Setup(std::move(setup), ifhandle.NewRequest());
  if (status != ZX_OK) {
    ifhandle.TakeChannel().reset();  // dispose of channel
  }
  callback(status, std::move(ifhandle));
}

fidl::InterfaceRequestHandler<fuchsia::netemul::network::NetworkContext>
NetworkContext::GetHandler() {
  return [this](fidl::InterfaceRequest<FNetworkContext> request) {
    bindings_.AddBinding(this, std::move(request), dispatcher_);
  };
}

zx::channel NetworkContext::ConnectDevfs() const {
  if (devfs_handler_) {
    zx::channel cli, req;
    zx::channel::create(0, &cli, &req);
    devfs_handler_(std::move(req));
    return cli;
  } else {
    return zx::channel();
  }
}

fidl::InterfaceHandle<fuchsia::net::tun::Control> NetworkContext::ConnectNetworkTun() const {
  fidl::InterfaceHandle<fuchsia::net::tun::Control> ret;
  if (network_tun_handler_) {
    network_tun_handler_(ret.NewRequest());
  }
  return ret;
}

}  // namespace netemul
