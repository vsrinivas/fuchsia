// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/fidl_interface_monitor.h"

#include "garnet/bin/mdns/service/mdns_fidl_util.h"
#include "lib/fxl/logging.h"

namespace mdns {

// static
std::unique_ptr<InterfaceMonitor> FidlInterfaceMonitor::Create(
    component::StartupContext* startup_context) {
  return std::unique_ptr<InterfaceMonitor>(
      new FidlInterfaceMonitor(startup_context));
}

FidlInterfaceMonitor::FidlInterfaceMonitor(
    component::StartupContext* startup_context)
    : binding_(this) {
  netstack_ = startup_context
                  ->ConnectToEnvironmentService<fuchsia::netstack::Netstack>();

  binding_.set_error_handler([this]() {
    binding_.set_error_handler(nullptr);
    binding_.Unbind();
    FXL_LOG(ERROR) << "Connection to netstack dropped.";
  });
  netstack_.events().InterfacesChanged =
      fit::bind_member(this, &FidlInterfaceMonitor::OnInterfacesChanged);

  netstack_->GetInterfaces(
      [this](fidl::VectorPtr<fuchsia::netstack::NetInterface> interfaces) {
        OnInterfacesChanged(std::move(interfaces));
      });
}

FidlInterfaceMonitor::~FidlInterfaceMonitor() {
  if (binding_.is_bound()) {
    binding_.set_error_handler(nullptr);
    binding_.Unbind();
  }
}

void FidlInterfaceMonitor::RegisterLinkChangeCallback(fit::closure callback) {
  link_change_callback_ = std::move(callback);
}

const std::vector<std::unique_ptr<InterfaceDescriptor>>&
FidlInterfaceMonitor::GetInterfaces() {
  return interfaces_;
}

void FidlInterfaceMonitor::OnInterfacesChanged(
    fidl::VectorPtr<fuchsia::netstack::NetInterface> interfaces) {
  bool link_change = false;

  for (const auto& if_info : *interfaces) {
    IpAddress address = MdnsFidlUtil::IpAddressFrom(&if_info.addr);

    if (!address.is_valid() || address.is_loopback() ||
        (if_info.flags & fuchsia::netstack::NetInterfaceFlagUp) == 0) {
      if (interfaces_.size() > if_info.id &&
          interfaces_[if_info.id] != nullptr) {
        // Interface went away.
        interfaces_[if_info.id] = nullptr;
        link_change = true;
      }

      continue;
    }

    // Make sure the |interfaces_| array is big enough.
    if (interfaces_.size() <= if_info.id) {
      interfaces_.resize(if_info.id + 1);
    }

    InterfaceDescriptor* existing = interfaces_[if_info.id].get();

    if (existing == nullptr) {
      // We don't have an |InterfaceDescriptor| for this interface yet. Add one.
      interfaces_[if_info.id].reset(
          new InterfaceDescriptor(address, if_info.name));
      link_change = true;
    } else if (existing->address_ != address ||
               existing->name_ != if_info.name) {
      // We have an |InterfaceDescriptor| for this interface, but it's
      // out-of-date. Update it.
      existing->address_ = address;
      existing->name_ = if_info.name;
      link_change = true;
    }
  }

  if (link_change && link_change_callback_) {
    link_change_callback_();
  }
}

}  // namespace mdns
