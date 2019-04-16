// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network.h"

#include <src/lib/fxl/memory/weak_ptr.h>

#include "fake_endpoint.h"
#include "interceptors/latency.h"
#include "interceptors/packet_loss.h"
#include "interceptors/reorder.h"
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
    if (interceptors_.empty()) {
      ForwardData(data, len, sender);
    } else {
      interceptors_.back()->Intercept(InterceptPacket(data, len, sender));
    }
  }

  std::vector<data::Consumer::Ptr>& sinks() { return sinks_; }
  std::vector<FakeEndpoint::Ptr>& fake_endpoints() { return fake_endpoints_; }

  void UpdateConfiguration(const Network::Config& config) {
    // first, flush all packets that are currently in interceptors:
    for (auto& i : interceptors_) {
      auto packets = i->Flush();
      for (auto& packet : packets) {
        ForwardData(packet.data().data(), packet.data().size(),
                    packet.origin());
      }
    }
    // clear all interceptors
    interceptors_.clear();

    // rebuild interceptors based on configuration

    // reordering interceptor:
    if (config.has_reorder()) {
      interceptors_.emplace_back(new interceptor::Reorder(
          config.reorder().store_buff, zx::msec(config.reorder().tick),
          MakeInterceptorCallback()));
    }
    // latency interceptor:
    if (config.has_latency()) {
      interceptors_.emplace_back(new interceptor::Latency(
          config.latency().average, config.latency().std_dev,
          MakeInterceptorCallback()));
    }
    // packet loss interceptor:
    if (config.has_packet_loss()) {
      if (config.packet_loss().is_random_rate()) {
        interceptors_.emplace_back(new interceptor::PacketLoss(
            config.packet_loss().random_rate(), MakeInterceptorCallback()));
      }
    }
  }

 private:
  Interceptor::ForwardPacketCallback MakeInterceptorCallback() {
    if (interceptors_.empty()) {
      // if no interceptors inside just forward packet
      return [this](InterceptPacket packet) {
        ForwardData(packet.data().data(), packet.data().size(),
                    packet.origin());
      };
    } else {
      // if interceptors already have members, point onto the back for chaining
      auto* next = interceptors_.back().get();
      return [this, next](InterceptPacket packet) {
        next->Intercept(std::move(packet));
      };
    }
  }

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
  std::vector<std::unique_ptr<Interceptor>> interceptors_;
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
  bus_->UpdateConfiguration(config_);
}

Network::~Network() = default;

void Network::Bind(fidl::InterfaceRequest<FNetwork> req) {
  bindings_.AddBinding(this, std::move(req), parent_->dispatcher());
}

void Network::GetConfig(Network::GetConfigCallback callback) {
  Config config;
  config_.Clone(&config);
  callback(std::move(config));
}

void Network::GetName(Network::GetNameCallback callback) { callback(name_); }

void Network::SetConfig(fuchsia::netemul::network::NetworkConfig config,
                        Network::SetConfigCallback callback) {
  if (!CheckConfig(config)) {
    callback(ZX_ERR_INVALID_ARGS);
    return;
  }
  config_ = std::move(config);
  bus_->UpdateConfiguration(config_);
  callback(ZX_OK);
}

zx_status_t Network::AttachEndpoint(std::string name) {
  data::Consumer::Ptr src;
  auto status = parent_->endpoint_manager().InstallSink(
      std::move(name), bus_->GetPointer(), &src);

  if (status != ZX_OK) {
    return status;
  } else if (!src) {
    return ZX_ERR_INTERNAL;
  }

  bus_->sinks().emplace_back(std::move(src));
  return ZX_OK;
}

void Network::AttachEndpoint(::std::string name,
                             Network::AttachEndpointCallback callback) {
  callback(AttachEndpoint(std::move(name)));
}

void Network::RemoveEndpoint(::std::string name,
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

bool Network::CheckConfig(const Config& config) {
  if (config.has_packet_loss()) {
    if (config.packet_loss().is_random_rate()) {
      // random rate packet loss must be unsigned byte less or equal to 100
      if (config.packet_loss().random_rate() > 100) {
        return false;
      }
    } else {
      // non random-rate packet loss not supported.
      return false;
    }
  }

  return true;
}

}  // namespace netemul
