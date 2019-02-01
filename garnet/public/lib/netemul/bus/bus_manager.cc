// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bus_manager.h"

namespace netemul {

void BusManager::Subscribe(std::string busName, std::string clientName,
                           ::fidl::InterfaceRequest<Bus::FBus> bus) {
  auto& b = GetBus(busName);
  b.Subscribe(clientName, std::move(bus));
}

Bus& BusManager::GetBus(const std::string& name) {
  auto f = buses_.find(name);
  if (f != buses_.end()) {
    return *f->second;
  }
  // bus doesn't exist yet, create it:
  auto bus = std::make_unique<Bus>(dispatcher_);
  auto ret =
      buses_.insert(std::pair<std::string, Bus::Ptr>(name, std::move(bus)));
  return *ret.first->second;
}

fidl::InterfaceRequestHandler<fuchsia::netemul::bus::BusManager>
BusManager::GetHandler() {
  return bindings_.GetHandler(this, dispatcher_);
}
}  // namespace netemul
