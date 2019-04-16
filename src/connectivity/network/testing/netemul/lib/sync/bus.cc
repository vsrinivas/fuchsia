// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bus.h"

#include <lib/async/cpp/task.h>
#include <zircon/assert.h>

#include "callback_watch.h"

namespace netemul {

class WaitForClientsWatch : public CallbackWatch<Bus::WaitForClientsCallback> {
 public:
  using MissingClientsCallback =
      fit::function<std::vector<std::string>(const std::vector<std::string>&)>;
  WaitForClientsWatch(std::vector<std::string> clients,
                      Bus::WaitForClientsCallback callback)
      : CallbackWatch(std::move(callback)), clients_(std::move(clients)) {}

  const std::vector<std::string>& clients() const { return clients_; }

  void OnTimeout() override {
    ZX_ASSERT(missing_callback_);
    FireCallback(false,
                 fidl::VectorPtr<std::string>(missing_callback_(clients_)));
  }

  void SetMissingClientsCallback(MissingClientsCallback callback) {
    missing_callback_ = std::move(callback);
  }

 private:
  std::vector<std::string> clients_;
  MissingClientsCallback missing_callback_;
};

class WaitForEventWatch : public CallbackWatch<Bus::WaitForEventCallback> {
 public:
  WaitForEventWatch(Bus::FEvent event, Bus::WaitForEventCallback callback)
      : CallbackWatch(std::move(callback)), event_(std::move(event)) {}

  void OnTimeout() override { FireCallback(false); }

  bool EventMatches(const Bus::FEvent& cmp) const {
    if (event_.has_code() && (!cmp.has_code() || cmp.code() != event_.code())) {
      return false;
    } else if (event_.has_message() &&
               (!cmp.has_message() || cmp.message() != event_.message())) {
      return false;
    } else if (event_.has_arguments() &&
               (!cmp.has_arguments() ||
                cmp.arguments().size() != event_.arguments().size() ||
                memcmp(cmp.arguments().data(), event_.arguments().data(),
                       event_.arguments().size()) != 0)) {
      return false;
    } else {
      return true;
    }
  }

 private:
  Bus::FEvent event_;
};

class BusBinding : public fuchsia::netemul::sync::Bus {
 public:
  using FBus = fuchsia::netemul::sync::Bus;
  using FEvent = fuchsia::netemul::sync::Event;
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

  void WaitForEvent(fuchsia::netemul::sync::Event data, int64_t timeout,
                    WaitForEventCallback callback) override {
    auto watcher = std::make_unique<WaitForEventWatch>(std::move(data),
                                                       std::move(callback));

    if (timeout > 0) {
      watcher->PostTimeout(binding_.dispatcher(), timeout);
    }
    event_watches_.push_back(std::move(watcher));
  }

  void WaitForClients(::std::vector<::std::string> clients, int64_t timeout,
                      WaitForClientsCallback callback) override {
    parent_->WaitForClients(std::move(clients), timeout, std::move(callback));
  }

  void OnBusData(const std::string& from, FEvent event) {
    if (from != client_name_) {
      // upon publishing any events on the bus, check all event waits
      for (auto i = event_watches_.begin(); i != event_watches_.end();) {
        if ((*i)->valid()) {
          if ((*i)->EventMatches(event)) {
            (*i)->FireCallback(true);
            i = event_watches_.erase(i);
          } else {
            ++i;
          }
        } else {
          i = event_watches_.erase(i);
        }
      }

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
  std::vector<std::unique_ptr<WaitForEventWatch>> event_watches_;
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

  // Whenever a new client attaches, we check the client watches to fire
  // anything pending
  for (auto i = client_watches_.begin(); i != client_watches_.end();) {
    if (CheckClientWatch(i->get())) {
      i = client_watches_.erase(i);
    } else {
      ++i;
    }
  }
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

void Bus::WaitForClients(std::vector<std::string> clients, int64_t timeout,
                         WaitForClientsCallback callback) {
  auto client_watch = std::make_unique<WaitForClientsWatch>(
      std::move(clients), std::move(callback));
  if (CheckClientWatch(client_watch.get())) {
    return;
  } else {
    if (timeout > 0) {
      client_watch->SetMissingClientsCallback(
          [this](const std::vector<std::string>& wait) {
            std::vector<std::string> missing;
            for (const auto& client : wait) {
              if (clients_.find(client) == clients_.end()) {
                missing.push_back(client);
              }
            }
            return missing;
          });
      client_watch->PostTimeout(dispatcher_, timeout);
    }
    client_watches_.push_back(std::move(client_watch));
  }
}

bool Bus::CheckClientWatch(WaitForClientsWatch* watch) {
  if (!watch->valid()) {
    // already fired
    return true;
  }
  for (const auto& cli : watch->clients()) {
    if (clients_.find(cli) == clients_.end()) {
      return false;
    }
  }
  watch->FireCallback(true, nullptr);
  return true;
}

Bus::~Bus() = default;

}  // namespace netemul
