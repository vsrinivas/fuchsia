// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network.h"
#include <lib/fxl/memory/weak_ptr.h>
#include "fake_endpoint.h"
#include "network_context.h"

namespace netemul {
namespace impl {

class NetworkBus : public data::BusConsumer {
 public:
  NetworkBus() : weak_ptr_factory_(this) {}
  ~NetworkBus() override = default;

  decltype(auto) GetPointer() { return weak_ptr_factory_.GetWeakPtr(); }

  void Consume(const void* data, size_t len,
               const data::Consumer::Ptr& sender) override {
    // TODO(brunodalbo) implement network condition interceptors
    // before flooding everyone, we will pass every packet through the data
    // interceptors
    // [...]
    ForwardData(data, len, sender);
  }

  std::vector<data::Consumer::Ptr>& sinks() { return sinks_; }
  std::vector<FakeEndpoint::Ptr>& fake_endpoints() { return fake_endpoints_; }

 private:
  void ForwardData(const void* data, size_t len,
                   const data::Consumer::Ptr& sender) {
    for (auto i = sinks_.begin(); i != sinks_.end();) {
      if (*i) {
        if (i->get() !=
            sender.get()) {  // flood all sinks different than sender
          (*i)->Consume(data, len);
        }
        ++i;
      } else {
        // sink has been free'd, erase it
        i = sinks_.erase(i);
      }
    }
  }

  fxl::WeakPtrFactory<data::BusConsumer> weak_ptr_factory_;
  std::vector<data::Consumer::Ptr> sinks_;
  std::vector<FakeEndpoint::Ptr> fake_endpoints_;
};

}  // namespace impl

Network::Network(NetworkContext* context, std::string name,
                 Network::Config config)
    : bus_(new impl::NetworkBus()),
      parent_(context),
      name_(std::move(name)),
      config_(std::move(config)) {
  bindings_.set_empty_set_handler([this]() {
    if (closed_callback_) {
      closed_callback_(*this);
    }
  });
}

Network::~Network() {}

fidl::InterfaceHandle<Network::FNetwork> Network::Bind() {
  return bindings_.AddBinding(this, parent_->dispatcher());
}

void Network::GetConfig(Network::GetConfigCallback callback) {
  Config config;
  config_.Clone(&config);
  callback(std::move(config));
}

void Network::GetName(Network::GetNameCallback callback) { callback(name_); }

void Network::SetConfig(fuchsia::netemul::network::NetworkConfig config,
                        Network::SetConfigCallback callback) {
  // we may want to validate the configuration and return errors in some cases:
  config_ = std::move(config);
  // TODO(brunodalbo) actually implement bad network emulation
  if (config_.has_reorder()) {
    fprintf(stderr, "Network reorder emulation not implemented\n");
  }
  if (config_.has_latency()) {
    fprintf(stderr, "Network latency emulation not implemented\n");
  }
  if (config_.has_packet_loss()) {
    fprintf(stderr, "Network packet loss not implemented\n");
  }
  callback(ZX_OK);
}

void Network::AttachEndpoint(::fidl::StringPtr name,
                             Network::AttachEndpointCallback callback) {
  data::Consumer::Ptr src;
  auto status =
      parent_->endpoint_manager().InstallSink(name, bus_->GetPointer(), &src);

  if (status == ZX_OK) {
    if (src) {
      bus_->sinks().emplace_back(std::move(src));
    } else {
      status = ZX_ERR_INTERNAL;
    }
  }
  callback(status);
}

void Network::RemoveEndpoint(::fidl::StringPtr name,
                             Network::RemoveEndpointCallback callback) {
  data::Consumer::Ptr src;
  auto status =
      parent_->endpoint_manager().RemoveSink(name, bus_->GetPointer(), &src);
  if (status == ZX_OK && src) {
    auto& sinks = bus_->sinks();
    for (auto i = sinks.begin(); i != sinks.end(); i++) {
      if (i->get() == src.get()) {
        sinks.erase(i);
        break;
      }
    }
  }
  callback(status);
}

void Network::CreateFakeEndpoint(
    fidl::InterfaceRequest<fuchsia::netemul::network::FakeEndpoint> ep) {
  FakeEndpoint::Ptr fep = std::make_unique<FakeEndpoint>(
      bus_->GetPointer(), std::move(ep), parent_->dispatcher());

  fep->SetOnDisconnected([this](const FakeEndpoint* ep) {
    // when endpoint is disconnected for whatever reason
    // we remove it from the bus
    auto& feps = bus_->fake_endpoints();
    for (auto i = feps.begin(); i != feps.end(); i++) {
      if (i->get() == ep) {
        feps.erase(i);
        break;
      }
    }
  });

  // save the sink in bus
  bus_->sinks().emplace_back(fep->GetPointer());
  // keep and save endpoint
  bus_->fake_endpoints().emplace_back(std::move(fep));
}

void Network::SetClosedCallback(Network::ClosedCallback cb) {
  closed_callback_ = std::move(cb);
}

}  // namespace netemul