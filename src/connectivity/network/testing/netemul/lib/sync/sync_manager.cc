// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync_manager.h"

namespace netemul {

void SyncManager::BusSubscribe(std::string busName, std::string clientName,
                               ::fidl::InterfaceRequest<Bus::FBus> bus) {
  auto& b = GetBus(busName);
  b.Subscribe(clientName, std::move(bus));
}

Bus& SyncManager::GetBus(const std::string& name) {
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

fidl::InterfaceRequestHandler<fuchsia::netemul::sync::SyncManager>
SyncManager::GetHandler() {
  return bindings_.GetHandler(this, dispatcher_);
}

void SyncManager::WaitForBarrierThreshold(
    ::std::string barrierName, uint32_t threshold, int64_t timeout,
    WaitForBarrierThresholdCallback callback) {
  auto barrier = counter_barriers_.find(barrierName);
  if (barrier == counter_barriers_.end()) {
    barrier =
        counter_barriers_
            .insert(std::make_pair(
                barrierName, std::make_unique<CounterBarrier>(dispatcher_)))
            .first;
  }

  barrier->second->AddWatch(threshold, timeout, std::move(callback));

  if (barrier->second->empty()) {
    counter_barriers_.erase(barrier);
  }
}

SyncManager::~SyncManager() = default;

}  // namespace netemul
