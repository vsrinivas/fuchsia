// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/mdns_transceiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include "apps/netconnector/src/mdns/mdns_addresses.h"
#include "apps/netstack/apps/include/netconfig.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace netconnector {
namespace mdns {

// static
const ftl::TimeDelta MdnsTransceiver::kMinAddressRecheckDelay =
    ftl::TimeDelta::FromSeconds(1);

// static
const ftl::TimeDelta MdnsTransceiver::kMaxAddressRecheckDelay =
    ftl::TimeDelta::FromSeconds(5 * 60);

MdnsTransceiver::MdnsTransceiver()
    : task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()) {}

MdnsTransceiver::~MdnsTransceiver() {}

void MdnsTransceiver::EnableInterface(const std::string& name,
                                      sa_family_t family) {
  enabled_interfaces_.emplace_back(name, family);
}

bool MdnsTransceiver::Start(
    const std::string& host_full_name,
    const InboundMessageCallback& inbound_message_callback) {
  FTL_DCHECK(host_full_name.size() > 0);
  FTL_DCHECK(inbound_message_callback);

  inbound_message_callback_ = inbound_message_callback;
  host_full_name_ = host_full_name;

  return FindNewInterfaces();
}

void MdnsTransceiver::Stop() {
  for (auto& interface : interfaces_) {
    interface->Stop();
  }
}

bool MdnsTransceiver::InterfaceEnabled(netc_if_info_t* if_info) {
  if ((if_info->flags & NETC_IFF_UP) == 0) {
    return false;
  }

  if (enabled_interfaces_.empty()) {
    return true;
  }

  for (auto& enabled_interface : enabled_interfaces_) {
    if (enabled_interface.name_.compare(if_info->name) == 0 &&
        enabled_interface.family_ == if_info->addr.ss_family) {
      return true;
    }
  }

  return false;
}

void MdnsTransceiver::SendMessage(DnsMessage* message,
                                  const SocketAddress& dest_address,
                                  uint32_t interface_index) {
  FTL_DCHECK(message);

  if (dest_address == MdnsAddresses::kV4Multicast) {
    for (auto& i : interfaces_) {
      i->SendMessage(message, dest_address);
    }

    return;
  }

  FTL_DCHECK(interface_index < interfaces_.size());
  interfaces_[interface_index]->SendMessage(message, dest_address);
}

bool MdnsTransceiver::FindNewInterfaces() {
  ftl::UniqueFD socket_fd = ftl::UniqueFD(socket(AF_INET, SOCK_DGRAM, 0));

  if (!socket_fd.is_valid()) {
    FTL_LOG(ERROR) << "Failed to open socket, errno " << errno;
    return false;
  }

  // Get network interface info.
  netc_get_if_info_t get_if_info;
  ssize_t size = ioctl_netc_get_if_info(socket_fd.get(), &get_if_info);
  if (size < 0) {
    FTL_LOG(ERROR) << "ioctl_netc_get_if_info failed, errno " << errno;
    return false;
  }

  bool recheck_addresses = false;

  if (get_if_info.n_info == 0) {
    recheck_addresses = true;
  }

  // Launch a transceiver for each new interface.
  for (uint32_t i = 0; i < get_if_info.n_info; ++i) {
    netc_if_info_t* if_info = &get_if_info.info[i];

    // We seem to get a good family value regardless of whether we have an IP
    // address, but we check anyway.
    if (if_info->addr.ss_family != AF_INET &&
        if_info->addr.ss_family != AF_INET6) {
      FTL_LOG(ERROR) << "Not starting mDNS for interface " << if_info->name
                     << ": unsupported address family "
                     << if_info->addr.ss_family;
      continue;
    }

    if (InterfaceEnabled(if_info)) {
      IpAddress address((struct sockaddr*)&if_info->addr);

      if (!AddressIsSet(address)) {
        recheck_addresses = true;
        continue;
      }

      if (InterfaceAlreadyFound(address)) {
        continue;
      }

      std::unique_ptr<MdnsInterfaceTransceiver> interface =
          MdnsInterfaceTransceiver::Create(*if_info, interfaces_.size());

      interface->Start(host_full_name_, inbound_message_callback_);

      for (auto& i : interfaces_) {
        if (i->name() == interface->name()) {
          i->SetAlternateAddress(host_full_name_, interface->address());
          interface->SetAlternateAddress(host_full_name_, i->address());
        }
      }

      interfaces_.push_back(std::move(interface));
    }
  }

  if (recheck_addresses) {
    task_runner_->PostDelayedTask([this]() { FindNewInterfaces(); },
                                  address_recheck_delay_);

    address_recheck_delay_ =
        std::min(address_recheck_delay_ * kAddressRecheckDelayMultiplier,
                 kMaxAddressRecheckDelay);
  }

  return true;
}

bool MdnsTransceiver::InterfaceAlreadyFound(const IpAddress& address) {
  for (auto& i : interfaces_) {
    if (i->address() == address) {
      return true;
    }
  }

  return false;
}

bool MdnsTransceiver::AddressIsSet(const IpAddress& address) {
  size_t word_count = address.word_count();
  const uint16_t* words = address.as_words();

  for (size_t i = 0; i < word_count; ++i, ++words) {
    if (*words != 0) {
      return true;
    }
  }

  return false;
}

}  // namespace mdns
}  // namespace netconnector
