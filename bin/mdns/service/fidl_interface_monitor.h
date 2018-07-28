// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_FIDL_INTERFACE_MONITOR_H_
#define GARNET_BIN_MDNS_SERVICE_FIDL_INTERFACE_MONITOR_H_

#include <memory>

#include <fuchsia/netstack/cpp/fidl.h>

#include "garnet/bin/mdns/service/interface_monitor.h"
#include "lib/component/cpp/startup_context.h"

namespace mdns {

// FIDL-based interface monitor implementation.
class FidlInterfaceMonitor : public InterfaceMonitor {
 public:
  static std::unique_ptr<InterfaceMonitor> Create(
      component::StartupContext* startup_context);

  FidlInterfaceMonitor(component::StartupContext* startup_context);

  ~FidlInterfaceMonitor();

  // InterfaceMonitor implementation.
  void RegisterLinkChangeCallback(fit::closure callback) override;

  const std::vector<std::unique_ptr<InterfaceDescriptor>>& GetInterfaces()
      override;

 private:
  void InterfacesChanged(
      fidl::VectorPtr<fuchsia::netstack::NetInterface> interfaces);
  fuchsia::netstack::NetstackPtr netstack_;
  fit::closure link_change_callback_;
  std::vector<std::unique_ptr<InterfaceDescriptor>> interfaces_;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_FIDL_INTERFACE_MONITOR_H_
