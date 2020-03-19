// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "endpoint.h"

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <unordered_set>
#include <vector>

#include "ethernet_client.h"
#include "ethertap_client.h"
#include "network_context.h"

namespace netemul {

namespace impl {
class EndpointImpl : public data::Consumer {
 public:
  explicit EndpointImpl(Endpoint::Config config)
      : config_(std::move(config)), weak_ptr_factory_(this) {}

  decltype(auto) GetPointer() { return weak_ptr_factory_.GetWeakPtr(); }

  void CloneConfig(Endpoint::Config* config) { config_.Clone(config); }

  zx_status_t Setup(const std::string& name, NetworkContext& context) {
    EthertapConfig config;
    if (config_.mac) {
      config_.mac->Clone(&config.tap_cfg.mac);
    } else {
      // if mac is not provided, random mac is assigned with a seed based on
      // name
      config.RandomLocalUnicast(name);
    }

    config.name = name;
    config.tap_cfg.mtu = config_.mtu;
    fuchsia::hardware::ethernet::MacAddress mac;
    config.tap_cfg.mac.Clone(&mac);
    config.tap_cfg.options = fuchsia::hardware::ethertap::OPT_REPORT_PARAM;
    config.devfs_root = context.ConnectDevfs();
    zx_status_t status = EthertapClient::Create(std::move(config), &ethertap_);
    if (status != ZX_OK) {
      return status;
    }

    auto devfs_root = context.ConnectDevfs();
    if (devfs_root.is_valid()) {
      ethernet_factory_ = std::make_unique<EthernetClientFactory>(
          EthernetClientFactory::kDevfsEthernetRoot, std::move(devfs_root));
    } else {
      ethernet_factory_ = std::make_unique<EthernetClientFactory>();
    }

    // We add a 2 min timeout to this so that it's easier to debug problems with enumerating devices
    // from devfs. By default, MountPointWithMAC would hang forever and would make debugging
    // problems here really hard.
    ethernet_mount_path_ = ethernet_factory_->MountPointWithMAC(mac, zx::sec(120));

    // can't find mount path for ethernet!!
    if (ethernet_mount_path_.empty()) {
      fprintf(stderr, "Failed to locate ethertap device %s %02X:%02X:%02X:%02X:%02X:%02X\n",
              name.c_str(), mac.octets[0], mac.octets[1], mac.octets[2], mac.octets[3],
              mac.octets[4], mac.octets[5]);
      return ZX_ERR_INTERNAL;
    }

    ethertap_->SetPacketCallback(
        [this](std::vector<uint8_t> data) { ForwardData(&data[0], data.size()); });

    ethertap_->SetPeerClosedCallback([this]() {
      if (closed_callback_) {
        closed_callback_();
      }
    });
    return ZX_OK;
  }

  std::vector<data::BusConsumer::Ptr>& sinks() { return sinks_; }

  void SetLinkUp(bool up) { ethertap_->SetLinkUp(up); }

  void SetClosedCallback(fit::closure cb) { closed_callback_ = std::move(cb); }

  fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> GetEthernetDevice() {
    fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> ret;
    if (ethernet_factory_) {
      if (ethernet_factory_->Connect(ethernet_mount_path_, ret.NewRequest()) != ZX_OK) {
        // if it didn't succeed, release the request and reset:
        ret = nullptr;
      }
    }
    return ret;
  }

  void ConnectEthernetDevice(zx::channel channel) {
    if (ethernet_factory_) {
      ethernet_factory_->Connect(
          ethernet_mount_path_,
          fidl::InterfaceRequest<fuchsia::hardware::ethernet::Device>(std::move(channel)));
    }
  }

  void Consume(const void* data, size_t len) override {
    auto status = ethertap_->Send(data, len);
    if (status != ZX_OK) {
      fprintf(stderr, "ethertap couldn't push data: %s\n", zx_status_get_string(status));
    }
  }

 private:
  void ForwardData(const void* data, size_t len) {
    auto self = weak_ptr_factory_.GetWeakPtr();
    for (auto i = sinks_.begin(); i != sinks_.end();) {
      if (*i) {
        (*i)->Consume(data, len, self);
        ++i;
      } else {
        // sink was free'd remove it from list
        i = sinks_.erase(i);
      }
    }
  }

  std::string ethernet_mount_path_;
  std::unique_ptr<EthernetClientFactory> ethernet_factory_;
  fit::closure closed_callback_;
  std::unique_ptr<EthertapClient> ethertap_;
  Endpoint::Config config_;
  std::vector<data::BusConsumer::Ptr> sinks_;
  fxl::WeakPtrFactory<data::Consumer> weak_ptr_factory_;
};
}  // namespace impl

Endpoint::Endpoint(NetworkContext* context, std::string name, Endpoint::Config config)
    : impl_(new impl::EndpointImpl(std::move(config))), parent_(context), name_(std::move(name)) {
  auto close_handler = [this]() {
    if (closed_callback_) {
      // bubble up the problem
      closed_callback_(*this);
    }
  };

  impl_->SetClosedCallback(close_handler);
  bindings_.set_empty_set_handler(close_handler);
}

zx_status_t Endpoint::Startup(NetworkContext& context) { return impl_->Setup(name_, context); }

Endpoint::~Endpoint() = default;

void Endpoint::GetConfig(Endpoint::GetConfigCallback callback) {
  Config config;
  impl_->CloneConfig(&config);
  callback(std::move(config));
}

void Endpoint::GetName(Endpoint::GetNameCallback callback) { callback(name_); }

void Endpoint::SetLinkUp(bool up) { impl_->SetLinkUp(up); }

void Endpoint::SetLinkUp(bool up, Endpoint::SetLinkUpCallback callback) {
  impl_->SetLinkUp(up);
  callback();
}

void Endpoint::GetEthernetDevice(GetEthernetDeviceCallback callback) {
  callback(impl_->GetEthernetDevice());
}

void Endpoint::GetProxy(fidl::InterfaceRequest<FProxy> proxy) {
  proxy_bindings_.AddBinding(this, std::move(proxy), parent_->dispatcher());
}

void Endpoint::ServeDevice(zx::channel channel) {
  impl_->ConnectEthernetDevice(std::move(channel));
}

void Endpoint::Bind(fidl::InterfaceRequest<FEndpoint> req) {
  bindings_.AddBinding(this, std::move(req), parent_->dispatcher());
}

zx_status_t Endpoint::InstallSink(data::BusConsumer::Ptr sink, data::Consumer::Ptr* src) {
  // just forbid install if sink is already in our list:
  auto& sinks = impl_->sinks();
  for (auto& i : sinks) {
    if (i.get() == sink.get()) {
      return ZX_ERR_ALREADY_BOUND;
    }
  }
  impl_->sinks().emplace_back(std::move(sink));
  *src = impl_->GetPointer();
  return ZX_OK;
}

zx_status_t Endpoint::RemoveSink(data::BusConsumer::Ptr sink, data::Consumer::Ptr* src) {
  auto& sinks = impl_->sinks();
  for (auto i = sinks.begin(); i != sinks.end(); i++) {
    if (i->get() == sink.get()) {
      sinks.erase(i);
      *src = impl_->GetPointer();
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

void Endpoint::SetClosedCallback(Endpoint::EndpointClosedCallback cb) {
  closed_callback_ = std::move(cb);
}

}  // namespace netemul
