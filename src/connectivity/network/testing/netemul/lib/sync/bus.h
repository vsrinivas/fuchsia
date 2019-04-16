// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_SYNC_BUS_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_SYNC_BUS_H_

#include <fuchsia/netemul/sync/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <unordered_map>

namespace netemul {

class BusBinding;
class WaitForClientsWatch;
class Bus {
 public:
  using FBus = fuchsia::netemul::sync::Bus;
  using WaitForClientsCallback =
      fuchsia::netemul::sync::Bus::WaitForClientsCallback;
  using WaitForEventCallback =
      fuchsia::netemul::sync::Bus::WaitForEventCallback;
  using FEvent = fuchsia::netemul::sync::Event;
  using Ptr = std::unique_ptr<Bus>;

  explicit Bus(async_dispatcher_t* dispatcher);
  ~Bus();

  void Subscribe(const std::string& clientName,
                 fidl::InterfaceRequest<FBus> request);

 protected:
  using ClientBinding = std::unique_ptr<BusBinding>;
  friend BusBinding;
  void Publish(Bus::FEvent data, const std::string& from);
  void NotifyClientDetached(const std::string& client);
  void NotifyClientAttached(const std::string& client);
  const std::unordered_map<std::string, ClientBinding>& clients();
  void WaitForClients(std::vector<std::string> clients, int64_t timeout,
                      WaitForClientsCallback callback);
  bool CheckClientWatch(WaitForClientsWatch* watch);

 private:
  async_dispatcher_t* dispatcher_;
  std::unordered_map<std::string, ClientBinding> clients_;
  std::vector<std::unique_ptr<WaitForClientsWatch>> client_watches_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_SYNC_BUS_H_
