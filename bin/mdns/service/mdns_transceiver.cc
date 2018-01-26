// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_transceiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include "garnet/bin/mdns/service/mdns_addresses.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

namespace mdns {

MdnsTransceiver::MdnsTransceiver() {}

MdnsTransceiver::~MdnsTransceiver() {}

void MdnsTransceiver::EnableInterface(const std::string& name,
                                      sa_family_t family) {
  enabled_interfaces_.emplace_back(name, family);
}

void MdnsTransceiver::Start(
    std::unique_ptr<InterfaceMonitor> interface_monitor,
    const LinkChangeCallback& link_change_callback,
    const InboundMessageCallback& inbound_message_callback) {
  FXL_DCHECK(interface_monitor);
  FXL_DCHECK(link_change_callback);
  FXL_DCHECK(inbound_message_callback);

  interface_monitor_ = std::move(interface_monitor);
  link_change_callback_ = link_change_callback;
  inbound_message_callback_ = inbound_message_callback;

  interface_monitor_->RegisterLinkChangeCallback(
      [this]() { FindNewInterfaces(); });

  FindNewInterfaces();
}

void MdnsTransceiver::Stop() {
  if (interface_monitor_) {
    interface_monitor_->RegisterLinkChangeCallback(nullptr);
  }

  for (auto& interface : interfaces_) {
    if (interface) {
      interface->Stop();
    }
  }
}

void MdnsTransceiver::SetHostFullName(const std::string& host_full_name) {
  FXL_DCHECK(!host_full_name.empty());

  host_full_name_ = host_full_name;

  for (auto& interface : interfaces_) {
    if (interface) {
      interface->SetHostFullName(host_full_name_);
    }
  }
}

bool MdnsTransceiver::InterfaceEnabled(
    const InterfaceDescriptor& interface_descr) {
  if (enabled_interfaces_.empty()) {
    return true;
  }

  for (auto& enabled_interface : enabled_interfaces_) {
    if (enabled_interface.name_ == interface_descr.name_ &&
        enabled_interface.family_ == interface_descr.address_.family()) {
      return true;
    }
  }

  return false;
}

void MdnsTransceiver::SendMessage(DnsMessage* message,
                                  const ReplyAddress& reply_address) {
  FXL_DCHECK(message);

  if (reply_address.socket_address() == MdnsAddresses::kV4Multicast) {
    for (auto& interface : interfaces_) {
      if (interface) {
        interface->SendMessage(message, reply_address.socket_address());
      }
    }

    return;
  }

  FXL_DCHECK(reply_address.interface_index() < interfaces_.size());
  interfaces_[reply_address.interface_index()]->SendMessage(
      message, reply_address.socket_address());
}

void MdnsTransceiver::FindNewInterfaces() {
  FXL_DCHECK(interface_monitor_);

  bool link_change = false;
  uint32_t index = 0;

  // Add and remove interface transceivers as appropriate.
  for (const auto& interface_descr : interface_monitor_->GetInterfaces()) {
    if (!interface_descr || !InterfaceEnabled(*interface_descr)) {
      if (MaybeRemoveInterfaceTransceiver(index)) {
        // Interface went away.
        link_change = true;
      }

      ++index;
      continue;
    }

    if (MaybeAddInterfaceTransceiver(index, *interface_descr)) {
      link_change = true;
    }

    ++index;
  }

  while (index < interfaces_.size()) {
    MaybeRemoveInterfaceTransceiver(index);
    ++index;
  }

  if (link_change && link_change_callback_) {
    link_change_callback_();
  }
}

bool MdnsTransceiver::MaybeAddInterfaceTransceiver(
    size_t index,
    const InterfaceDescriptor& interface_descr) {
  if (interfaces_.size() > index && interfaces_[index] != nullptr) {
    // Interface transceiver already exists.
    return false;
  }

  std::unique_ptr<MdnsInterfaceTransceiver> interface =
      MdnsInterfaceTransceiver::Create(interface_descr.address_,
                                       interface_descr.name_, index);

  if (!interface->Start(inbound_message_callback_)) {
    // Couldn't start the transceiver.
    return false;
  }

  if (!host_full_name_.empty()) {
    interface->SetHostFullName(host_full_name_);
  }

  for (auto& i : interfaces_) {
    if (i != nullptr && i->name() == interface->name()) {
      i->SetAlternateAddress(host_full_name_, interface->address());
      interface->SetAlternateAddress(host_full_name_, i->address());
    }
  }

  if (interfaces_.size() <= index) {
    interfaces_.resize(index + 1);
  }

  interfaces_[index] = std::move(interface);

  return true;
}

bool MdnsTransceiver::MaybeRemoveInterfaceTransceiver(size_t index) {
  if (interfaces_.size() <= index || interfaces_[index] == nullptr) {
    // No such interface transceiver.
    return false;
  }

  // Stop and destroy the interface transceiver.
  interfaces_[index]->Stop();
  interfaces_[index] = nullptr;

  // Shrink |interfaces_| to remove nulls at the end.
  size_t new_size = interfaces_.size();
  while (new_size != 0 && interfaces_[new_size - 1] == nullptr) {
    --new_size;
  }

  interfaces_.resize(new_size);

  return true;
}

}  // namespace mdns
