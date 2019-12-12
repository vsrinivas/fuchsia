// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "netstack_intermediary.h"

#include <lib/async/default.h>
#include <lib/fit/bridge.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/device/ethernet.h>
#include <zircon/status.h>

#include <vector>

#include "src/lib/fxl/logging.h"

static constexpr netemul::EthernetConfig eth_config = {.nbufs = 256, .buff_size = 2048};

NetstackIntermediary::NetstackIntermediary(NetworkMap mac_network_mapping)
    : NetstackIntermediary(std::move(mac_network_mapping), sys::ComponentContext::Create()) {}

NetstackIntermediary::NetstackIntermediary(NetworkMap mac_network_mapping,
                                           std::unique_ptr<sys::ComponentContext> context)
    : mac_network_mapping_(std::move(mac_network_mapping)),
      context_(std::move(context)),
      executor_(async_get_default_dispatcher()) {
  context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void NetstackIntermediary::SetInterfaceAddress(uint32_t nicid, fuchsia::net::IpAddress addr,
                                               uint8_t prefixLen,
                                               SetInterfaceAddressCallback callback) {
  fuchsia::netstack::NetErr err = {
      .status = fuchsia::netstack::Status::OK,
      .message = "",
  };
  callback(err);
}

void NetstackIntermediary::AddEthernetDevice(
    std::string topological_path, fuchsia::netstack::InterfaceConfig interfaceConfig,
    fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> device,
    AddEthernetDeviceCallback callback) {
  fuchsia::netemul::network::EndpointConfig config;
  config.mtu = 1500;
  config.backing = fuchsia::netemul::network::EndpointBacking::ETHERTAP;

  NetworkBinding network_binding;
  network_binding.first =
      std::make_unique<netemul::EthernetClient>(async_get_default_dispatcher(), device.Bind());

  size_t index = guest_client_endpoints_.size();
  guest_client_endpoints_.push_back(std::move(network_binding));

  // Create a FakeEndpoint and an EthernetClient.  The EthernetClient serves
  // as an interface between the guest's ethernet device and the FakeEndpoint
  // which is linked into the netemul virtual network.
  executor_.schedule_task(
      fit::make_promise([this, index]() mutable {
        // Get the MAC address from the ethernet device and determine which ethertap network it
        // should be connected to.
        fit::bridge<std::string> bridge;
        auto& [eth_client, fake_ep] = guest_client_endpoints_[index];
        eth_client->device()->GetInfo([this, completer = std::move(bridge.completer)](
                                          fuchsia::hardware::ethernet::Info info) mutable {
          NetworkMap::iterator iterator = mac_network_mapping_.find(info.mac.octets);
          if (iterator != mac_network_mapping_.end()) {
            completer.complete_ok(iterator->second);
            return;
          }

          char buffer[kMacAddrStringLength + 1];
          for (uint8_t i = 0; i < info.mac.octets.size(); i++) {
            sprintf(&buffer[3 * i], "%02X:", info.mac.octets[i]);
          }
          buffer[kMacAddrStringLength] = '\0';

          FXL_LOG(ERROR) << "No network specified for " << buffer;
          completer.complete_error();
        });
        return bridge.consumer.promise();
      })
          .and_then([this](const std::string& network_name) { return GetNetwork(network_name); })
          .and_then([this, index](fidl::InterfaceHandle<fuchsia::netemul::network::Network>& net) {
            auto& [eth_client, fake_ep] = guest_client_endpoints_[index];
            net.Bind()->CreateFakeEndpoint(fake_ep.NewRequest());
            return SetupEthClient(eth_client);
          })
          .and_then([this, index, callback = std::move(callback)]() mutable {
            // The FakeEndpoint's OnData method fires when new data is observed
            // on the netemul virtual network.
            auto& [eth_client, fake_ep] = guest_client_endpoints_[index];
            fake_ep.events().OnData = [this, index](std::vector<uint8_t> data) {
              auto& [eth_client, fake_ep] = guest_client_endpoints_[index];
              eth_client->Send(data.data(), data.size());
            };
            fake_ep.set_error_handler([](zx_status_t status) {
              FXL_LOG(INFO) << "FakeEndpoint encountered error: " << zx_status_get_string(status);
            });

            // EthernetClient's DataCallback fires when the guest is trying to
            // write to the netemul virtual network.
            eth_client->SetDataCallback([this, index](const void* data, size_t len) {
              auto& [eth_client, fake_ep] = guest_client_endpoints_[index];
              std::vector<uint8_t> input_data(static_cast<const uint8_t*>(data),
                                              static_cast<const uint8_t*>(data) + len);
              fake_ep->Write(std::move(input_data));
            });
            eth_client->SetPeerClosedCallback(
                [] { FXL_LOG(INFO) << "EthernetClient peer closed."; });

            callback(1);
          })
          .or_else([]() mutable { FXL_CHECK(false) << "Failed to add ethernet device."; })
          .wrap_with(scope_));
}

fit::promise<fidl::InterfaceHandle<fuchsia::netemul::network::Network>>
NetstackIntermediary::GetNetwork(std::string network_name) {
  fit::bridge<fidl::InterfaceHandle<fuchsia::netemul::network::Network>> bridge;

  auto netc = std::make_shared<fidl::InterfacePtr<fuchsia::netemul::network::NetworkContext>>();
  auto net_mgr = std::make_shared<fuchsia::netemul::network::NetworkManagerPtr>();

  context_->svc()->Connect(netc->NewRequest());

  (*netc)->GetNetworkManager(net_mgr->NewRequest());
  (*net_mgr)->GetNetwork(
      network_name, [completer = std::move(bridge.completer), network_name, netc, net_mgr](
                        fidl::InterfaceHandle<fuchsia::netemul::network::Network> net) mutable {
        if (net.is_valid()) {
          completer.complete_ok(std::move(net));
        } else {
          FXL_LOG(ERROR) << "No such network: \"" << network_name << "\"";
          completer.complete_error();
        }
      });

  return bridge.consumer.promise();
}

fit::promise<> NetstackIntermediary::SetupEthClient(
    const std::unique_ptr<netemul::EthernetClient>& eth_client) {
  fit::bridge<> bridge;
  eth_client->Setup(eth_config,
                    [completer = std::move(bridge.completer)](zx_status_t status) mutable {
                      if (status == ZX_OK) {
                        completer.complete_ok();
                      } else {
                        FXL_LOG(ERROR) << "EthernetClient setup failed with " << status;
                        completer.complete_error();
                      }
                    });

  return bridge.consumer.promise();
}
