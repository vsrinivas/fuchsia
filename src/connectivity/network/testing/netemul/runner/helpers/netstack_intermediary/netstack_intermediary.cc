// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "netstack_intermediary.h"

#include <lib/async/default.h>
#include <lib/fit/bridge.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/logging.h>
#include <zircon/device/ethernet.h>
#include <zircon/status.h>

#include <vector>

static constexpr netemul::EthernetConfig eth_config = {.buff_size = 2048,
                                                       .nbufs = 256};

NetstackIntermediary::NetstackIntermediary(std::string network_name)
    : NetstackIntermediary(std::move(network_name),
                           sys::ComponentContext::Create()) {}

NetstackIntermediary::NetstackIntermediary(
    std::string network_name, std::unique_ptr<sys::ComponentContext> context)
    : network_name_(std::move(network_name)),
      context_(std::move(context)),
      executor_(async_get_default_dispatcher()) {
  context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void NetstackIntermediary::SetInterfaceAddress(
    uint32_t nicid, fuchsia::net::IpAddress addr, uint8_t prefixLen,
    SetInterfaceAddressCallback callback) {
  fuchsia::netstack::NetErr err = {
      .status = fuchsia::netstack::Status::OK,
      .message = "",
  };
  callback(err);
}

void NetstackIntermediary::AddEthernetDevice(
    std::string topological_path,
    fuchsia::netstack::InterfaceConfig interfaceConfig,
    fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> device,
    AddEthernetDeviceCallback callback) {
  fuchsia::netemul::network::EndpointConfig config;
  config.mtu = 1500;
  config.backing = fuchsia::netemul::network::EndpointBacking::ETHERTAP;

  eth_client_ = std::make_unique<netemul::EthernetClient>(
      async_get_default_dispatcher(), device.Bind());

  // Create a FakeEndpoint and an EthernetClient.  The EthernetClient serves
  // as an interface between the guest's ethernet device and the FakeEndpoint
  // which is linked into the netemul virtual network.
  executor_.schedule_task(
      GetNetwork(network_name_)
          .and_then(
              [this](fidl::InterfaceHandle<fuchsia::netemul::network::Network>&
                         net) mutable {
                return SetupEthClient(std::move(net));
              })
          .and_then([this, callback = std::move(callback)](
                        const zx_status_t& status) mutable {
            // The FakeEndpoint's OnData method fires when new data is observed
            // on the netemul virtual network.
            fake_ep_.events().OnData = [this](std::vector<uint8_t> data) {
              eth_client_->Send(data.data(), data.size());
            };
            fake_ep_.set_error_handler([this](zx_status_t status) {
              FXL_LOG(INFO) << "FakeEndpoint encountered error: "
                            << zx_status_get_string(status);
            });

            // EthernetClient's DataCallback fires when the guest is trying to
            // write to the netemul virtual network.
            eth_client_->SetDataCallback([this](const void* data, size_t len) {
              std::vector<uint8_t> input_data(
                  static_cast<const uint8_t*>(data),
                  static_cast<const uint8_t*>(data) + len);
              fake_ep_->Write(std::move(input_data));
            });
            eth_client_->SetPeerClosedCallback(
                [] { FXL_LOG(INFO) << "EthernetClient peer closed."; });

            callback(1);
          })
          .or_else([]() mutable {
            FXL_CHECK(false) << "Failed to add ethernet device.";
          })
          .wrap_with(scope_));
}

fit::promise<fidl::InterfaceHandle<fuchsia::netemul::network::Network>>
NetstackIntermediary::GetNetwork(std::string network_name) {
  fit::bridge<fidl::InterfaceHandle<fuchsia::netemul::network::Network>> bridge;

  auto netc = std::make_shared<
      fidl::InterfacePtr<fuchsia::netemul::network::NetworkContext>>();
  auto net_mgr =
      std::make_shared<fuchsia::netemul::network::NetworkManagerPtr>();

  context_->svc()->Connect(netc->NewRequest());

  (*netc)->GetNetworkManager(net_mgr->NewRequest());
  (*net_mgr)->GetNetwork(
      network_name,
      [this, completer = std::move(bridge.completer), network_name, netc,
       net_mgr](fidl::InterfaceHandle<fuchsia::netemul::network::Network>
                    net) mutable {
        if (net.is_valid()) {
          completer.complete_ok(std::move(net));
        } else {
          FXL_LOG(ERROR) << "No such network: " << network_name;
          completer.complete_error();
        }
      });

  return bridge.consumer.promise();
}

fit::promise<zx_status_t> NetstackIntermediary::SetupEthClient(
    fidl::InterfaceHandle<fuchsia::netemul::network::Network> net) {
  fit::bridge<zx_status_t> bridge;

  net.Bind()->CreateFakeEndpoint(fake_ep_.NewRequest());

  eth_client_->Setup(eth_config, [completer = std::move(bridge.completer)](
                                     zx_status_t status) mutable {
    if (status == ZX_OK) {
      completer.complete_ok(status);
    } else {
      FXL_LOG(ERROR) << "EthernetClient setup failed with " << status;
      completer.complete_error();
    }
  });

  return bridge.consumer.promise();
}
