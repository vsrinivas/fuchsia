// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/fidl_interface_monitor.h"

#include "garnet/bin/mdns/service/mdns_fidl_util.h"
#include "lib/fxl/logging.h"

namespace mdns {

// static
std::unique_ptr<InterfaceMonitor> FidlInterfaceMonitor::Create(
    app::ApplicationContext* application_context) {
  return std::unique_ptr<InterfaceMonitor>(
      new FidlInterfaceMonitor(application_context));
}

FidlInterfaceMonitor::FidlInterfaceMonitor(
    app::ApplicationContext* application_context)
    : binding_(this) {
  netstack_ =
      application_context->ConnectToEnvironmentService<netstack::Netstack>();

  fidl::InterfaceHandle<netstack::NotificationListener> listener_handle;

  binding_.Bind(&listener_handle);
  binding_.set_connection_error_handler([this]() {
    binding_.set_connection_error_handler(nullptr);
    binding_.Close();
    FXL_LOG(ERROR) << "Connection to netstack dropped.";
  });

  netstack_->RegisterListener(std::move(listener_handle));

  netstack_->GetInterfaces(
      [this](fidl::Array<netstack::NetInterfacePtr> interfaces) {
        OnInterfacesChanged(std::move(interfaces));
      });
}

FidlInterfaceMonitor::~FidlInterfaceMonitor() {
  if (binding_.is_bound()) {
    binding_.set_connection_error_handler(nullptr);
    binding_.Close();
  }
}

void FidlInterfaceMonitor::RegisterLinkChangeCallback(
    const fxl::Closure& callback) {
  link_change_callback_ = callback;
}

const std::vector<std::unique_ptr<InterfaceDescriptor>>&
FidlInterfaceMonitor::GetInterfaces() {
  return interfaces_;
}

void FidlInterfaceMonitor::OnInterfacesChanged(
    fidl::Array<netstack::NetInterfacePtr> interfaces) {
  bool link_change = false;

  for (const auto& if_info : interfaces) {
    IpAddress address = MdnsFidlUtil::IpAddressFrom(if_info->addr.get());

    if (!address.is_valid() || address.is_loopback() ||
        (if_info->flags & netstack::NetInterfaceFlagUp) == 0) {
      if (interfaces_.size() > if_info->id &&
          interfaces_[if_info->id] != nullptr) {
        // Interface went away.
        interfaces_[if_info->id] = nullptr;
        link_change = true;
      }

      continue;
    }

    // Make sure the |interfaces_| array is big enough.
    if (interfaces_.size() <= if_info->id) {
      interfaces_.resize(if_info->id + 1);
    }

    // Add a descriptor if we don't already have one.
    if (interfaces_[if_info->id] == nullptr) {
      interfaces_[if_info->id].reset(
          new InterfaceDescriptor(address, if_info->name));
      link_change = true;
    }
  }

  if (link_change && link_change_callback_) {
    link_change_callback_();
  }
}

}  // namespace mdns
