// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "netstack_intermediary.h"

#include <lib/async/default.h>
#include <lib/fpromise/bridge.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/device/ethernet.h>
#include <zircon/status.h>

#include <vector>

static constexpr netemul::EthernetConfig eth_config = {.nbufs = 256, .buff_size = 2048};
static constexpr uint64_t kMaxPendingWrites = 1024;

NetstackIntermediary::NetstackIntermediary(NetworkMap mac_network_mapping)
    : NetstackIntermediary(std::move(mac_network_mapping),
                           sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

NetstackIntermediary::NetstackIntermediary(NetworkMap mac_network_mapping,
                                           std::unique_ptr<sys::ComponentContext> context)
    : mac_network_mapping_(std::move(mac_network_mapping)),
      context_(std::move(context)),
      executor_(async_get_default_dispatcher()),
      pending_writes_(0) {
  context_->outgoing()->AddPublicService(netstack_.GetHandler(this));
  context_->outgoing()->AddPublicService(control_.GetHandler(this));
}

void NetstackIntermediary::CreateNetwork(
    fuchsia::net::virtualization::Config config,
    fidl::InterfaceRequest<fuchsia::net::virtualization::Network> network) {
  switch (config.Which()) {
    case fuchsia::net::virtualization::Config::Tag::Invalid:
      network.Close(ZX_ERR_INTERNAL);
      return;
    case fuchsia::net::virtualization::Config::Tag::kBridged:
      if (!config.bridged().IsEmpty()) {
        network.Close(ZX_ERR_INTERNAL);
        return;
      }
      break;
    default:
      network.Close(ZX_ERR_INTERNAL);
      return;
  }
  network_.AddBinding(this, std::move(network), async_get_default_dispatcher());
}

void NetstackIntermediary::AddPort(
    fidl::InterfaceHandle<fuchsia::hardware::network::Port> port,
    fidl::InterfaceRequest<fuchsia::net::virtualization::Interface> interface) {
  fpromise::bridge<MacAddr, zx_status_t> bridge;
  std::shared_ptr completer =
      std::make_shared<decltype(bridge.completer)>(std::move(bridge.completer));
  auto cb = [completer](zx_status_t status) { completer->complete_error(status); };

  fidl::InterfacePtr port_proxy = port.Bind();
  port_proxy.set_error_handler(cb);
  fidl::InterfacePtr<fuchsia::hardware::network::MacAddressing> mac_addressing;
  mac_addressing.set_error_handler(cb);
  port_proxy->GetMac(mac_addressing.NewRequest());
  mac_addressing->GetUnicastAddress(
      [completer](fuchsia::net::MacAddress mac) { completer->complete_ok(mac.octets); });
  fpromise::promise<void> task =
      bridge.consumer.promise()
          .and_then(fit::bind_member(this, &NetstackIntermediary::GetNetwork))
          .then([port = std::move(port_proxy), interface = std::move(interface)](
                    fpromise::result<fidl::InterfaceHandle<fuchsia::netemul::network::Network>,
                                     zx_status_t>& network) mutable {
            if (network.is_error()) {
              interface.Close(network.error());
            } else {
              fidl::InterfacePtr network_proxy = network.value().Bind();
              network_proxy.set_error_handler([](zx_status_t status) {
                /* nothing can be done, |interface| is passed off to network */
              });
              network_proxy->AddPort(port.Unbind(), std::move(interface));
            }
          })
          // Keep |mac_addressing| alive; otherwise the callback won't fire.
          .inspect([mac_addressing = std::move(mac_addressing)](const fpromise::result<>&) {})
          .wrap_with(scope_);
  executor_.schedule_task(std::move(task));
}

void NetstackIntermediary::AddEthernetDevice(
    std::string topological_path, fuchsia::netstack::InterfaceConfig interfaceConfig,
    fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> device,
    AddEthernetDeviceCallback callback) {
  fpromise::bridge<MacAddr, zx_status_t> bridge;
  std::shared_ptr completer =
      std::make_shared<decltype(bridge.completer)>(std::move(bridge.completer));
  fidl::InterfacePtr device_proxy = device.Bind();
  device_proxy.set_error_handler(
      [completer](zx_status_t status) { completer->complete_error(status); });
  device_proxy->GetInfo([completer](fuchsia::hardware::ethernet::Info info) mutable {
    completer->complete_ok(info.mac.octets);
  });
  fpromise::promise<> task =
      bridge.consumer.promise()
          .and_then(fit::bind_member(this, &NetstackIntermediary::GetNetwork))
          // Nesting callbacks here to keep |device| alive.
          .and_then([this, device_proxy = std::move(device_proxy)](
                        fidl::InterfaceHandle<fuchsia::netemul::network::Network>& net) mutable {
            // Create a FakeEndpoint and an EthernetClient.  The EthernetClient serves
            // as an interface between the guest's ethernet device and the FakeEndpoint
            // which is linked into the netemul virtual network.
            std::unique_ptr eth_client = std::make_unique<netemul::EthernetClient>(
                async_get_default_dispatcher(), std::move(device_proxy));
            fpromise::bridge<void, zx_status_t> bridge;
            eth_client->Setup(
                eth_config, [completer = std::move(bridge.completer)](zx_status_t status) mutable {
                  if (status == ZX_OK) {
                    completer.complete_ok();
                  } else {
                    completer.complete_error(status);
                  }
                });
            // Nesting callbacks here to keep |net| alive.
            return bridge.consumer.promise().and_then([this, net = net.Bind(),
                                                       eth_client =
                                                           std::move(eth_client)]() mutable {
              const size_t index = guest_client_endpoints_.size();

              NetworkBinding& network_binding = guest_client_endpoints_.emplace_back(
                  std::move(eth_client),
                  fidl::InterfacePtr<fuchsia::netemul::network::FakeEndpoint>{});
              network_binding.first->SetDataCallback([this, index](const void* data, size_t len) {
                std::vector<uint8_t> input_data(static_cast<const uint8_t*>(data),
                                                static_cast<const uint8_t*>(data) + len);
                // Don't enqueue too many write requests.
                if (pending_writes_ < kMaxPendingWrites) {
                  pending_writes_++;
                  guest_client_endpoints_[index].second->Write(std::move(input_data),
                                                               [this]() { pending_writes_--; });
                }
              });
              network_binding.first->SetPeerClosedCallback(
                  [] { FX_LOGS(INFO) << "EthernetClient peer closed"; });
              network_binding.second.set_error_handler([](zx_status_t status) {
                FX_LOGS(INFO) << "FakeEndpoint encountered error: " << zx_status_get_string(status);
              });

              net->CreateFakeEndpoint(network_binding.second.NewRequest());

              ReadGuestEp(index);

              return fpromise::ok(index);
            });
          })
          .then([callback = std::move(callback)](fpromise::result<size_t, zx_status_t>& index) {
            if (index.is_error()) {
              callback(fuchsia::netstack::Netstack_AddEthernetDevice_Result::WithErr(
                  int32_t(index.error())));
            } else {
              callback(fuchsia::netstack::Netstack_AddEthernetDevice_Result::WithResponse(
                  fuchsia::netstack::Netstack_AddEthernetDevice_Response(
                      static_cast<uint32_t>(index.value()))));
            }
          })
          .wrap_with(scope_);

  executor_.schedule_task(std::move(task));
}

fpromise::promise<fidl::InterfaceHandle<fuchsia::netemul::network::Network>, zx_status_t>
NetstackIntermediary::GetNetwork(const MacAddr& octets) {
  NetworkMap::iterator iterator = mac_network_mapping_.find(octets);
  if (iterator == mac_network_mapping_.end()) {
    return fpromise::make_result_promise<fidl::InterfaceHandle<fuchsia::netemul::network::Network>,
                                         zx_status_t>(fpromise::error(ZX_ERR_NOT_FOUND));
  }
  fpromise::bridge<fidl::InterfaceHandle<fuchsia::netemul::network::Network>, zx_status_t> bridge;
  std::shared_ptr completer =
      std::make_shared<decltype(bridge.completer)>(std::move(bridge.completer));
  auto cb = [completer](zx_status_t status) { completer->complete_error(status); };

  fidl::InterfacePtr<fuchsia::netemul::network::NetworkContext> network_context;
  network_context.set_error_handler(cb);
  if (zx_status_t status = context_->svc()->Connect(network_context.NewRequest());
      status != ZX_OK) {
    return fpromise::make_result_promise<fidl::InterfaceHandle<fuchsia::netemul::network::Network>,
                                         zx_status_t>(fpromise::error(status));
  }

  fidl::InterfacePtr<fuchsia::netemul::network::NetworkManager> network_manager;
  network_manager.set_error_handler(cb);
  network_context->GetNetworkManager(network_manager.NewRequest());

  network_manager->GetNetwork(
      iterator->second,
      [completer](fidl::InterfaceHandle<fuchsia::netemul::network::Network> net) mutable {
        completer->complete_ok(std::move(net));
      });
  // Keep |network_manager| alive; otherwise the callback won't fire.
  return bridge.consumer.promise().inspect(
      [network_manager = std::move(network_manager)](
          const fpromise::result<fidl::InterfaceHandle<fuchsia::netemul::network::Network>,
                                 zx_status_t>&) {});
}

void NetstackIntermediary::ReadGuestEp(size_t index) {
  guest_client_endpoints_[index].second->Read(
      [this, index](std::vector<uint8_t> data, uint64_t _dropped) {
        guest_client_endpoints_[index].first->Send(data.data(), static_cast<uint16_t>(data.size()));
        ReadGuestEp(index);
      });
}
