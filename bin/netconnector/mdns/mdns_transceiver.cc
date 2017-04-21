// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/mdns_transceiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include "apps/netstack/apps/include/netconfig.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace netconnector {
namespace mdns {

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
  FTL_DCHECK(inbound_message_callback);

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

  // Launch a transceiver for each interface.
  uint32_t interface_index = 0;
  for (uint32_t i = 0; i < get_if_info.n_info; ++i) {
    netc_if_info_t* if_info = &get_if_info.info[i];

    if (InterfaceEnabled(if_info)) {
      std::unique_ptr<MdnsInterfaceTransceiver> interface =
          MdnsInterfaceTransceiver::Create(*if_info, interface_index);

      interface->Start(host_full_name, inbound_message_callback);

      for (auto& i : interfaces_) {
        if (i->name() == interface->name()) {
          i->SetAlternateAddress(host_full_name, interface->address());
          interface->SetAlternateAddress(host_full_name, i->address());
        }
      }

      interfaces_.push_back(std::move(interface));
      ++interface_index;
    }
  }

  return true;
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
  FTL_DCHECK(interface_index < interfaces_.size());
  interfaces_[interface_index]->SendMessage(message, dest_address);
}

}  // namespace mdns
}  // namespace netconnector
