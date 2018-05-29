// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_FIDL_INTERFACE_MONITOR_H_
#define GARNET_BIN_MDNS_SERVICE_FIDL_INTERFACE_MONITOR_H_

#include <memory>

#include <netstack/cpp/fidl.h>
#include "garnet/bin/mdns/service/interface_monitor.h"
#include "lib/app/cpp/startup_context.h"

namespace mdns {

// FIDL-based interface monitor implementation.
class FidlInterfaceMonitor : public netstack::NotificationListener,
                             public InterfaceMonitor {
 public:
  static std::unique_ptr<InterfaceMonitor> Create(
      fuchsia::sys::StartupContext* startup_context);

  FidlInterfaceMonitor(fuchsia::sys::StartupContext* startup_context);

  ~FidlInterfaceMonitor();

  // InterfaceMonitor implementation.
  void RegisterLinkChangeCallback(const fxl::Closure& callback) override;

  const std::vector<std::unique_ptr<InterfaceDescriptor>>& GetInterfaces()
      override;

 private:
  // NotificationListener implementation.
  void OnInterfacesChanged(
      fidl::VectorPtr<netstack::NetInterface> interfaces) override;

  netstack::NetstackPtr netstack_;
  fidl::Binding<netstack::NotificationListener> binding_;
  fxl::Closure link_change_callback_;
  std::vector<std::unique_ptr<InterfaceDescriptor>> interfaces_;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_FIDL_INTERFACE_MONITOR_H_
