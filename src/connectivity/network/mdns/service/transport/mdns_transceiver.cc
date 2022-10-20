// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/transport/mdns_transceiver.h"

#include <arpa/inet.h>
#include <fuchsia/hardware/network/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/common/mdns_addresses.h"
#include "src/connectivity/network/mdns/service/common/mdns_fidl_util.h"

namespace mdns {

MdnsTransceiver::MdnsTransceiver() = default;

MdnsTransceiver::~MdnsTransceiver() = default;

void MdnsTransceiver::Start(fuchsia::net::interfaces::WatcherPtr watcher,
                            fit::closure link_change_callback,
                            InboundMessageCallback inbound_message_callback,
                            InterfaceTransceiverCreateFunction transceiver_factory) {
  FX_DCHECK(watcher);
  FX_DCHECK(link_change_callback);
  FX_DCHECK(inbound_message_callback);
  FX_DCHECK(transceiver_factory);

  interface_watcher_ = std::move(watcher);
  link_change_callback_ = std::move(link_change_callback);
  inbound_message_callback_ = [this, callback = std::move(inbound_message_callback)](
                                  std::unique_ptr<DnsMessage> message,
                                  const ReplyAddress& reply_address) {
    if (!IsLocalInterfaceAddress(reply_address.socket_address().address())) {
      callback(std::move(message), reply_address);
    }
  };
  transceiver_factory_ = std::move(transceiver_factory);

  interface_watcher_->Watch(fit::bind_member<&MdnsTransceiver::OnInterfacesEvent>(this));
}

void MdnsTransceiver::Stop() {
  interface_watcher_ = nullptr;

  for (const auto& [address, interface] : interface_transceivers_by_address_) {
    if (interface) {
      interface->Stop();
    }
  }
}

bool MdnsTransceiver::HasInterfaces() { return !interface_transceivers_by_address_.empty(); }

MdnsInterfaceTransceiver* MdnsTransceiver::GetInterfaceTransceiver(const inet::IpAddress& address) {
  auto iter = interface_transceivers_by_address_.find(address);
  return iter == interface_transceivers_by_address_.end() ? nullptr : iter->second.get();
}

void MdnsTransceiver::SendMessage(const DnsMessage& message, const ReplyAddress& reply_address) {
  if (reply_address.is_multicast_placeholder()) {
    for (const auto& [address, interface] : interface_transceivers_by_address_) {
      FX_DCHECK(interface);
      if ((reply_address.media() == Media::kBoth || reply_address.media() == interface->media()) &&
          (reply_address.ip_versions() == IpVersions::kBoth ||
           reply_address.ip_versions() == interface->IpVersions())) {
        interface->SendMessage(message, reply_address.socket_address());
      }
    }

    return;
  }

  auto interface_transceiver = GetInterfaceTransceiver(reply_address.interface_address());
  if (interface_transceiver != nullptr) {
    interface_transceiver->SendMessage(message, reply_address.socket_address());
  }
}

void MdnsTransceiver::LogTraffic() {
  for (const auto& [address, interface] : interface_transceivers_by_address_) {
    FX_DCHECK(interface);
    interface->LogTraffic();
  }
}

std::vector<HostAddress> MdnsTransceiver::LocalHostAddresses() {
  std::vector<HostAddress> result;

  for (auto& pair : interface_transceivers_by_address_) {
    result.emplace_back(pair.first, pair.second->id(),
                        zx::sec(mdns::DnsResource::kShortTimeToLive));
  }

  return result;
}

bool MdnsTransceiver::StartInterfaceTransceivers(const net::interfaces::Properties& properties) {
  if (properties.is_loopback() || !properties.online()) {
    return false;
  }

  Media media;
  switch (properties.device_class().device()) {
    case fuchsia::hardware::network::DeviceClass::WLAN:
    case fuchsia::hardware::network::DeviceClass::WLAN_AP:
      media = Media::kWireless;
      break;
    case fuchsia::hardware::network::DeviceClass::ETHERNET:
    case fuchsia::hardware::network::DeviceClass::PPP:
    case fuchsia::hardware::network::DeviceClass::BRIDGE:
    case fuchsia::hardware::network::DeviceClass::VIRTUAL:
      media = Media::kWired;
      break;
  }

  std::vector<inet::IpAddress> addresses;
  std::transform(
      properties.addresses().begin(), properties.addresses().end(), std::back_inserter(addresses),
      [](const auto& address) { return MdnsFidlUtil::IpAddressFrom(address.addr().addr); });

  bool started = false;
  for (const auto& net_interfaces_addr : properties.addresses()) {
    const inet::IpAddress addr = MdnsFidlUtil::IpAddressFrom(net_interfaces_addr.addr().addr);
    if (addr.is_v6() && !addr.is_link_local()) {
      // Do not stand up transceivers for non-local V6 addresses.
      continue;
    }

    const uint64_t id = properties.id();
    // NB: fuchsia.net.interfaces/Properties reports IDs as uint64_t but we store them as uint32
    // for usage in POSIX APIs in transceivers. Ensure that the conversion is valid here.
    FX_DCHECK(id <= std::numeric_limits<uint32_t>::max()) << id << " doesn't fit in a uint32";
    started |= EnsureInterfaceTransceiver(addr, addresses, static_cast<uint32_t>(id), media,
                                          properties.name());
  }

  return started;
}

bool MdnsTransceiver::StopInterfaceTransceiver(const inet::IpAddress& address) {
  auto nh = interface_transceivers_by_address_.extract(address);
  if (nh.empty()) {
    return false;
  }
  nh.mapped()->Stop();
  return true;
}

bool MdnsTransceiver::OnInterfaceDiscovered(fuchsia::net::interfaces::Properties discovered,
                                            const char* event_type) {
  std::optional<net::interfaces::Properties> validated_properties =
      net::interfaces::Properties::VerifyAndCreate(std::move(discovered));
  if (!validated_properties) {
    FX_LOGS(ERROR) << "malformed properties found in " << event_type
                   << " event from fuchsia.net.interfaces/Watcher";
    return false;
  }

  const auto& [iter, inserted] =
      interface_properties_.emplace(validated_properties->id(), std::move(*validated_properties));
  const auto& properties = iter->second;
  if (!inserted) {
    FX_LOGS(ERROR) << "duplicate interface (id=" << properties.id() << ") found in " << event_type
                   << " event from fuchsia.net.interfaces/Watcher";
    return false;
  }

  return StartInterfaceTransceivers(properties);
}

void MdnsTransceiver::OnInterfacesEvent(fuchsia::net::interfaces::Event event) {
  interface_watcher_->Watch(fit::bind_member<&MdnsTransceiver::OnInterfacesEvent>(this));

  bool link_change = false;

  switch (event.Which()) {
    case fuchsia::net::interfaces::Event::kExisting:
      link_change = OnInterfaceDiscovered(std::move(event.existing()), "Existing");
      break;
    case fuchsia::net::interfaces::Event::kAdded:
      link_change = OnInterfaceDiscovered(std::move(event.added()), "Added");
      break;
    case fuchsia::net::interfaces::Event::kChanged: {
      fuchsia::net::interfaces::Properties& change = event.changed();
      if (!change.has_id()) {
        FX_LOGS(ERROR)
            << "missing interface ID in Changed event from fuchsia.net.interfaces/Watcher";
        return;
      }
      auto it = interface_properties_.find(change.id());
      if (it == interface_properties_.end()) {
        FX_LOGS(ERROR) << "unknown interface in Changed event from fuchsia.net.interfaces/Watcher";
        return;
      }
      auto& properties = it->second;

      if (!properties.Update(&change)) {
        FX_LOGS(ERROR) << "failed to update interface properties with Changed event from "
                          "fuchsia.net.interfaces/Watcher";
        return;
      }

      if (properties.is_loopback()) {
        return;
      }

      // If online changed from false to true, start interfaces transceivers on all current
      // addresses; else if online changed from true to false, remove all interface transceivers on
      // previous addresses (if addresses also changed), or current addresses.
      //
      // Otherwise online hasn't changed, but if online is true and addresses has changed, then stop
      // all transceivers running on addresses that have been removed and ensure there is one
      // running for every current address.
      if (change.has_online()) {
        if (properties.online()) {
          link_change = StartInterfaceTransceivers(properties);
        } else {
          auto& addresses_to_remove =
              change.has_addresses() ? change.addresses() : properties.addresses();
          for (const auto& address : addresses_to_remove) {
            link_change |=
                StopInterfaceTransceiver(MdnsFidlUtil::IpAddressFrom(address.addr().addr));
          }
        }
      } else if (change.has_addresses() && properties.online()) {
        std::unordered_set<inet::IpAddress> addresses;
        addresses.reserve(properties.addresses().size());
        for (const auto& address : properties.addresses()) {
          addresses.emplace(MdnsFidlUtil::IpAddressFrom(address.addr().addr));
        }
        for (const auto& address : change.addresses()) {
          const auto previous_address = MdnsFidlUtil::IpAddressFrom(address.addr().addr);
          // This could be a lookup, but we might as well erase from the set to keep the set as
          // small as possible.
          if (addresses.erase(previous_address) == 0) {
            link_change |= StopInterfaceTransceiver(previous_address);
          }
        }

        link_change |= StartInterfaceTransceivers(properties);
      }
      break;
    }
    case fuchsia::net::interfaces::Event::kRemoved: {
      auto nh = interface_properties_.extract(event.removed());
      if (nh.empty()) {
        FX_LOGS(WARNING)
            << "Removed event for unknown interface from fuchsia.net.interfaces/Watcher";
      } else {
        for (const auto& address : nh.mapped().addresses()) {
          link_change |= StopInterfaceTransceiver(MdnsFidlUtil::IpAddressFrom(address.addr().addr));
        }
      }
      break;
    }
    case fuchsia::net::interfaces::Event::kIdle:
      break;
    case fuchsia::net::interfaces::Event::Invalid:
      FX_LOGS(WARNING) << "invalid event received from fuchsia.net.interfaces/Watcher";
      break;
  }

  if (link_change && link_change_callback_) {
    link_change_callback_();
  }
}

bool MdnsTransceiver::EnsureInterfaceTransceiver(
    const inet::IpAddress& address, const std::vector<inet::IpAddress>& interface_addresses,
    uint32_t id, Media media, const std::string& name) {
  if (!address.is_valid()) {
    return false;
  }

  bool result_on_fail = false;

  auto iter = interface_transceivers_by_address_.find(address);
  if (iter != interface_transceivers_by_address_.end()) {
    FX_DCHECK(iter->second);
    auto& existing = iter->second;
    FX_DCHECK(existing->address() == address);

    if (existing->name() == name && existing->id() == id) {
      // An interface transceiver already exists for this address, so we're done.
      existing->SetInterfaceAddresses(interface_addresses);
      return false;
    }

    // We have an interface transceiver for this address, but its name or id
    // don't match. Destroy it and create a new one.
    interface_transceivers_by_address_.erase(iter);
    result_on_fail = true;
  }

  auto interface_transceiver = transceiver_factory_(address, name, id, media);

  if (!interface_transceiver->Start(inbound_message_callback_.share())) {
    // Couldn't start the transceiver.
    return result_on_fail;
  }

  interface_transceiver->SetInterfaceAddresses(interface_addresses);

  interface_transceivers_by_address_.emplace(address, std::move(interface_transceiver));

  return true;
}

bool MdnsTransceiver::IsLocalInterfaceAddress(const inet::IpAddress& address) {
  return interface_transceivers_by_address_.find(
             address.is_mapped_from_v4() ? address.mapped_v4_address() : address) !=
         interface_transceivers_by_address_.end();
}

}  // namespace mdns
