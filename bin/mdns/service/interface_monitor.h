// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_INTERFACE_MONITOR_H_
#define GARNET_BIN_MDNS_SERVICE_INTERFACE_MONITOR_H_

#include <memory>
#include <string>
#include <vector>

#include <lib/fit/function.h>

#include "garnet/lib/inet/ip_address.h"

namespace mdns {

// Describes an interface.
struct InterfaceDescriptor {
  InterfaceDescriptor(inet::IpAddress address, const std::string& name)
      : address_(address), name_(name) {}

  inet::IpAddress address_;
  std::string name_;
};

// Abstract base class for network interface monitoring.
class InterfaceMonitor {
 public:
  virtual ~InterfaceMonitor() {}

  // Registers a callback to be called when a link change occurs.
  virtual void RegisterLinkChangeCallback(fit::closure callback) = 0;

  // Returns the current collection of viable interfaces.
  virtual const std::vector<std::unique_ptr<InterfaceDescriptor>>&
  GetInterfaces() = 0;

 protected:
  InterfaceMonitor() {}
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_INTERFACE_MONITOR_H_
