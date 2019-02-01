// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bus.h"
#include <zircon/assert.h>

namespace netemul {

class BusBinding : public fuchsia::netemul::bus::Bus {
 public:
  using FBus = fuchsia::netemul::bus::Bus;
  using FEvent = fuchsia::netemul::bus::Event;
  BusBinding(std::string name, ::netemul::Bus* parent,
             async_dispatcher_t* dispatcher,
             fidl::InterfaceRequest<FBus> request)
      : client_name_(std::move(name)),
        parent_(parent),
        binding_(this, std::move(request), dispatcher) {}

  void Publish(FEvent data) override {
    parent_->Publish(std::move(data), client_name_);
  }

  void EnsurePublish(FEvent data,
                     FBus::EnsurePublishCallback callback) override {
    parent_->Publish(std::move(data), client_name_);
    callback();
  }

  void GetClients(GetClientsCallback callback) override {
    std::vector<std::string> data;
    const auto& clients = parent_->clients();
    data.reserve(clients.size());
    for (const auto& x : clients) {
      data.push_back(x.first);
    }
    callback(std::move(data));
  }

  void OnBusData(const std::string& from, FEvent event) {
    if (from != client_name_) {
      binding_.events().OnBusData(std::move(event));
    }
  }

  void OnClientAttached(const std::string& client) {
    if (client != client_name_) {
      binding_.events().OnClientAttached(client);
    }
  }

  void OnClientDetached(const std::string& client) {
    if (client != client_name_) {
      binding_.events().OnClientDetached(client);
    }
  }

  void SetErrorHandler(fit::function<void(zx_status_t)> handler) {
    binding_.set_error_handler(std::move(handler));
  }

 private:
  const std::string client_name_;
  // Pointer to parent bus. Not owned.
  ::netemul::Bus* parent_;
  fidl::Binding<FBus> binding_;
};

Bus::Bus(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

void Bus::Subscribe(const std::string& clientName,
                    fidl::InterfaceRequest<Bus::FBus> request) {
  // reject if clientName is already in clients
  if (clients_.find(clientName) != clients_.end()) {
    return;
  }

  auto binding = std::make_unique<BusBinding>(clientName, this, dispatcher_,
                                              std::move(request));
  binding->SetErrorHandler([this, clientName](zx_status_t err) {
    // notify other clients this client is gone
    NotifyClientDetached(clientName);

    clients_.erase(clientName);
  });

  // notify other clients of new client
  NotifyClientAttached(clientName);

  clients_.insert(
      std::pair<std::string, ClientBinding>(clientName, std::move(binding)));
}

void Bus::NotifyClientAttached(const std::string& client) {
  for (const auto& cli : clients_) {
    cli.second->OnClientAttached(client);
  }
}

void Bus::NotifyClientDetached(const std::string& client) {
  for (const auto& cli : clients_) {
    cli.second->OnClientDetached(client);
  }
}

const std::unordered_map<std::string, Bus::ClientBinding>& Bus::clients() {
  return clients_;
}

void Bus::Publish(Bus::FEvent data, const std::string& from) {
  for (const auto& cli : clients_) {
    Bus::FEvent clone;
    ZX_ASSERT(data.Clone(&clone) == ZX_OK);
    cli.second->OnBusData(from, std::move(clone));
  }
}

Bus::~Bus() = default;

}  // namespace netemul
