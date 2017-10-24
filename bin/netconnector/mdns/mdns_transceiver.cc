// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/mdns_transceiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include "garnet/bin/media/util/fidl_publisher.h"
#include "garnet/bin/netconnector/mdns/mdns_addresses.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

namespace netconnector {
namespace mdns {

// static
const fxl::TimeDelta MdnsTransceiver::kMinAddressRecheckDelay =
    fxl::TimeDelta::FromSeconds(1);

// static
const fxl::TimeDelta MdnsTransceiver::kMaxAddressRecheckDelay =
    fxl::TimeDelta::FromSeconds(5 * 60);

MdnsTransceiver::MdnsTransceiver()
    : task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()),
      application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  netstack_ =
      application_context_->ConnectToEnvironmentService<netstack::Netstack>();
}

MdnsTransceiver::~MdnsTransceiver() {}

void MdnsTransceiver::EnableInterface(const std::string& name,
                                      sa_family_t family) {
  enabled_interfaces_.emplace_back(name, family);
}

void MdnsTransceiver::Start(
    const InboundMessageCallback& inbound_message_callback) {
  FXL_DCHECK(inbound_message_callback);

  inbound_message_callback_ = inbound_message_callback;

  FindNewInterfaces();
}

void MdnsTransceiver::Stop() {
  for (auto& interface : interfaces_) {
    interface->Stop();
  }
}

void MdnsTransceiver::SetHostFullName(const std::string& host_full_name) {
  FXL_DCHECK(!host_full_name.empty());

  host_full_name_ = host_full_name;

  for (auto& i : interfaces_) {
    i->SetHostFullName(host_full_name_);
  }
}

bool MdnsTransceiver::InterfaceEnabled(const netstack::NetInterface* if_info) {
  if ((if_info->flags & netstack::NetInterfaceFlagUp) == 0) {
    return false;
  }

  IpAddress addr(if_info->addr.get());
  if (addr.is_loopback()) {
    return false;
  }

  if (enabled_interfaces_.empty()) {
    return true;
  }

  for (auto& enabled_interface : enabled_interfaces_) {
    if (enabled_interface.name_ == if_info->name &&
        enabled_interface.family_ == addr.family()) {
      return true;
    }
  }

  return false;
}

void MdnsTransceiver::SendMessage(DnsMessage* message,
                                  const ReplyAddress& reply_address) {
  FXL_DCHECK(message);

  if (reply_address.socket_address() == MdnsAddresses::kV4Multicast) {
    for (auto& i : interfaces_) {
      i->SendMessage(message, reply_address.socket_address());
    }

    return;
  }

  FXL_DCHECK(reply_address.interface_index() < interfaces_.size());
  interfaces_[reply_address.interface_index()]->SendMessage(
      message, reply_address.socket_address());
}

void MdnsTransceiver::FindNewInterfaces() {
  netstack_->GetInterfaces(
      [this](fidl::Array<netstack::NetInterfacePtr> interfaces) {
        bool recheck_addresses = false;

        if (interfaces.size() == 0) {
          recheck_addresses = true;
        }

        // Launch a transceiver for each new interface.
        for (const auto& if_info : interfaces) {
          if (if_info->addr->family ==
              netstack::NetAddressFamily::UNSPECIFIED) {
            recheck_addresses = true;
            continue;
          }

          if (InterfaceEnabled(if_info.get())) {
            IpAddress address(if_info->addr.get());

            if (InterfaceAlreadyFound(address)) {
              continue;
            }

            std::unique_ptr<MdnsInterfaceTransceiver> interface =
                MdnsInterfaceTransceiver::Create(if_info.get(),
                                                 interfaces_.size());

            if (!interface->Start(inbound_message_callback_)) {
              continue;
            }

            if (!host_full_name_.empty()) {
              interface->SetHostFullName(host_full_name_);
            }

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
      });
}

bool MdnsTransceiver::InterfaceAlreadyFound(const IpAddress& address) {
  for (auto& i : interfaces_) {
    if (i->address() == address) {
      return true;
    }
  }

  return false;
}

}  // namespace mdns
}  // namespace netconnector
