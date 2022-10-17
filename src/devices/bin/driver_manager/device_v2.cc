// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_v2.h"

#include <lib/driver2/node_add_args.h>

namespace dfv2 {

zx::result<std::unique_ptr<Device>> Device::CreateAndServe(
    std::string topological_path, std::string name, uint64_t device_symbol,
    async_dispatcher_t* dispatcher, component::OutgoingDirectory* outgoing,
    compat::DeviceServer server, dfv2::NodeManager* manager, dfv2::DriverHost* driver_host) {
  auto device = std::make_unique<Device>();
  // Serve our compat service in the outgoing directory.
  device->server_ = std::move(server);
  device->server_->Serve(dispatcher, outgoing);

  // Create the node.
  device->node_ = std::make_shared<dfv2::Node>(topological_path, std::vector<dfv2::Node*>(),
                                               manager, dispatcher, driver_host);

  // Manually make the offer for the compat service, because we need to set the
  // source to "driver_manager".
  auto offer = driver::MakeOffer<fuchsia_driver_compat::Service>(name);
  auto child = fuchsia_component_decl::ChildRef();
  child.name() = "driver_manager";
  offer.service()->source() = fuchsia_component_decl::Ref::WithChild(std::move(child));

  // Set the node's offers.
  std::vector<fuchsia_component_decl::wire::Offer> offers;
  offers.push_back(fidl::ToWire(device->node_->arena(), offer));
  device->node_->set_offers(std::move(offers));

  // Set the node's symbols.
  auto symbol = fuchsia_driver_framework::wire::NodeSymbol::Builder(device->node_->arena());
  symbol.address(device_symbol);
  symbol.name(device->node_->arena(), "fuchsia.compat.device/Device");
  std::vector<fuchsia_driver_framework::wire::NodeSymbol> symbols;
  symbols.push_back(symbol.Build());
  device->node_->set_symbols(std::move(symbols));

  return zx::ok(std::move(device));
}

}  // namespace dfv2
