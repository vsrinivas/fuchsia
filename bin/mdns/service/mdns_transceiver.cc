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

  interface_monitor_->RegisterLinkChangeCallback([this]() { OnLinkChange(); });

  OnLinkChange();
}

void MdnsTransceiver::Stop() {
  if (interface_monitor_) {
    interface_monitor_->RegisterLinkChangeCallback(nullptr);
  }

  for (auto& interface : interface_transceivers_) {
    if (interface) {
      interface->Stop();
    }
  }
}

void MdnsTransceiver::SetHostFullName(const std::string& host_full_name) {
  FXL_DCHECK(!host_full_name.empty());

  host_full_name_ = host_full_name;

  for (auto& interface : interface_transceivers_) {
    if (interface) {
      interface->SetHostFullName(host_full_name_);
    }
  }
}

MdnsInterfaceTransceiver* MdnsTransceiver::GetInterfaceTransceiver(
    size_t index) {
  return interface_transceivers_.size() > index
             ? interface_transceivers_[index].get()
             : nullptr;
}

void MdnsTransceiver::SetInterfaceTransceiver(
    size_t index,
    std::unique_ptr<MdnsInterfaceTransceiver> interface_transceiver) {
  if (!interface_transceiver) {
    if (!GetInterfaceTransceiver(index)) {
      return;
    }

    interface_transceivers_[index] = nullptr;

    // Shrink |interface_transceivers_| to remove nulls at the end.
    size_t new_size = interface_transceivers_.size();
    while (new_size != 0 && interface_transceivers_[new_size - 1] == nullptr) {
      --new_size;
    }

    interface_transceivers_.resize(new_size);

    return;
  }

  if (interface_transceivers_.size() <= index) {
    interface_transceivers_.resize(index + 1);
  }

  interface_transceivers_[index] = std::move(interface_transceiver);
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
    for (auto& interface : interface_transceivers_) {
      if (interface) {
        interface->SendMessage(message, reply_address.socket_address());
      }
    }

    return;
  }

  auto interface_transceiver =
      GetInterfaceTransceiver(reply_address.interface_index());
  if (interface_transceiver != nullptr) {
    interface_transceiver->SendMessage(message, reply_address.socket_address());
  }
}

void MdnsTransceiver::LogTraffic() {
  for (auto& interface : interface_transceivers_) {
    if (interface) {
      interface->LogTraffic();
    }
  }
}

void MdnsTransceiver::OnLinkChange() {
  FXL_DCHECK(interface_monitor_);

  bool link_change = false;
  uint32_t index = 0;

  // Add and remove interface transceivers as appropriate.
  for (const auto& interface_descr : interface_monitor_->GetInterfaces()) {
    auto interface_transceiver = GetInterfaceTransceiver(index);

    if (!interface_descr || !InterfaceEnabled(*interface_descr)) {
      if (interface_transceiver != nullptr) {
        // Interface went away.
        RemoveInterfaceTransceiver(index);
        link_change = true;
      }

      ++index;
      continue;
    }

    if (interface_transceiver == nullptr) {
      // New interface.
      AddInterfaceTransceiver(index, *interface_descr);
      link_change = true;
    } else if (interface_transceiver->name() != interface_descr->name_ ||
               interface_transceiver->address() != interface_descr->address_) {
      // Existing interface has wrong name and/or address.
      ReplaceInterfaceTransceiver(index, *interface_descr);
      link_change = true;
    }

    ++index;
  }

  for (; index < interface_transceivers_.size(); ++index) {
    if (GetInterfaceTransceiver(index) != nullptr) {
      // Interface went away.
      RemoveInterfaceTransceiver(index);
    }
  }

  if (link_change && link_change_callback_) {
    link_change_callback_();
  }
}  // namespace mdns

void MdnsTransceiver::AddInterfaceTransceiver(
    size_t index,
    const InterfaceDescriptor& interface_descr) {
  FXL_DCHECK(GetInterfaceTransceiver(index) == nullptr);

  auto interface_transceiver = MdnsInterfaceTransceiver::Create(
      interface_descr.address_, interface_descr.name_, index);

  if (!interface_transceiver->Start(inbound_message_callback_)) {
    // Couldn't start the transceiver.
    return;
  }

  if (!host_full_name_.empty()) {
    interface_transceiver->SetHostFullName(host_full_name_);
  }

  for (auto& i : interface_transceivers_) {
    if (i != nullptr && i->name() == interface_transceiver->name()) {
      i->SetAlternateAddress(host_full_name_, interface_transceiver->address());
      interface_transceiver->SetAlternateAddress(host_full_name_, i->address());
    }
  }

  SetInterfaceTransceiver(index, std::move(interface_transceiver));
}

void MdnsTransceiver::ReplaceInterfaceTransceiver(
    size_t index,
    const InterfaceDescriptor& interface_descr) {
  auto interface_transceiver = GetInterfaceTransceiver(index);
  FXL_DCHECK(interface_transceiver);

  bool address_changed =
      interface_transceiver->address() != interface_descr.address_;

  // If the address has changed, send a message invalidating the old address.
  if (address_changed) {
    interface_transceiver->SendAddressGoodbye();
  }

  // Replace the interface transceiver with a new one.
  RemoveInterfaceTransceiver(index);
  AddInterfaceTransceiver(index, interface_descr);

  // If the address has changed, send a message with the new address.
  if (address_changed) {
    interface_transceiver = GetInterfaceTransceiver(index);
    FXL_DCHECK(interface_transceiver);
    interface_transceiver->SendAddress();
  }
}

void MdnsTransceiver::RemoveInterfaceTransceiver(size_t index) {
  auto interface_transceiver = GetInterfaceTransceiver(index);
  FXL_DCHECK(interface_transceiver);

  // Stop and destroy the interface transceiver.
  interface_transceiver->Stop();
  SetInterfaceTransceiver(index, nullptr);
}

}  // namespace mdns
