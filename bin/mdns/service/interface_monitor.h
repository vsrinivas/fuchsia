// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "garnet/bin/mdns/service/ip_address.h"
#include "lib/fxl/functional/closure.h"

namespace mdns {

// Describes an interface.
struct InterfaceDescriptor {
  InterfaceDescriptor(IpAddress address, const std::string& name)
      : address_(address), name_(name) {}

  IpAddress address_;
  std::string name_;
};

// Abstract base class for network interface monitoring.
class InterfaceMonitor {
 public:
  virtual ~InterfaceMonitor() {}

  // Registers a callback to be called when a link change occurs.
  virtual void RegisterLinkChangeCallback(const fxl::Closure& callback) = 0;

  // Returns the current collection of viable interfaces.
  virtual const std::vector<std::unique_ptr<InterfaceDescriptor>>&
  GetInterfaces() = 0;

 protected:
  InterfaceMonitor() {}
};

}  // namespace mdns
