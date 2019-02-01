// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_BUS_BUS_H_
#define LIB_NETEMUL_BUS_BUS_H_

#include <fuchsia/netemul/bus/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <unordered_map>

namespace netemul {

class BusBinding;
class Bus {
 public:
  using FBus = fuchsia::netemul::bus::Bus;
  using FEvent = fuchsia::netemul::bus::Event;
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

 private:
  async_dispatcher_t* dispatcher_;
  std::unordered_map<std::string, ClientBinding> clients_;
};

}  // namespace netemul

#endif  // LIB_NETEMUL_BUS_BUS_H_
