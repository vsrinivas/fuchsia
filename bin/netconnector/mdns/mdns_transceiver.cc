// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/mdns_transceiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include "garnet/bin/netconnector/mdns/mdns_addresses.h"
#include "garnet/go/src/netstack/apps/include/netconfig.h"
#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "garnet/bin/media/util/fidl_publisher.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

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
  netstack_ = application_context_->ConnectToEnvironmentService<netstack::Netstack>();
}

MdnsTransceiver::~MdnsTransceiver() {}

void MdnsTransceiver::EnableInterface(const std::string& name,
                                      sa_family_t family) {
  enabled_interfaces_.emplace_back(name, family);
}

bool MdnsTransceiver::Start(
    const std::string& host_full_name,
    const InboundMessageCallback& inbound_message_callback) {
  FXL_DCHECK(host_full_name.size() > 0);
  FXL_DCHECK(inbound_message_callback);

  inbound_message_callback_ = inbound_message_callback;
  host_full_name_ = host_full_name;

  return FindNewInterfaces();
}

void MdnsTransceiver::Stop() {
  for (auto& interface : interfaces_) {
    interface->Stop();
  }
}

bool MdnsTransceiver::InterfaceEnabled(const netstack::NetInterface* if_info) {
  if ((if_info->flags & netstack::NetInterfaceFlagUp) == 0) {
    return false;
  }

  if (enabled_interfaces_.empty()) {
    return true;
  }

  IpAddress addr(if_info->addr.get());
  for (auto& enabled_interface : enabled_interfaces_) {
    if (enabled_interface.name_ == if_info->name &&
        enabled_interface.family_ == addr.family()) {
      return true;
    }
  }

  return false;
}

void MdnsTransceiver::SendMessage(DnsMessage* message,
                                  const SocketAddress& dest_address,
                                  uint32_t interface_index) {
  FXL_DCHECK(message);

  if (dest_address == MdnsAddresses::kV4Multicast) {
    for (auto& i : interfaces_) {
      i->SendMessage(message, dest_address);
    }

    return;
  }

  FXL_DCHECK(interface_index < interfaces_.size());
  interfaces_[interface_index]->SendMessage(message, dest_address);
}

bool MdnsTransceiver::FindNewInterfaces() {
  netstack_->GetInterfaces(
      [this](fidl::Array<netstack::NetInterfacePtr> interfaces) {
        bool recheck_addresses = false;

        if (interfaces.size() == 0) {
          recheck_addresses = true;
        }

        // Launch a transceiver for each new interface.
        for (const auto& if_info : interfaces) {
          // We seem to get a good family value regardless of whether we have an
          // IP
          // address, but we check anyway.
          if (if_info->addr->family == netstack::NetAddressFamily::UNSPECIFIED) {
            FXL_LOG(ERROR) << "Not starting mDNS for interface "
                           << if_info->name << ": unspecified address family";
            continue;
          }

          if (InterfaceEnabled(if_info.get())) {
            IpAddress address(if_info->addr.get());

            // TODO(mpcomplete): I don't think this is necessary - unset
            // addresses should be UNSPECIFIED.
            if (!AddressIsSet(address)) {
              recheck_addresses = true;
              continue;
            }

            if (InterfaceAlreadyFound(address)) {
              continue;
            }

            std::unique_ptr<MdnsInterfaceTransceiver> interface =
                MdnsInterfaceTransceiver::Create(if_info.get(), interfaces_.size());

            if (!interface->Start(host_full_name_, inbound_message_callback_)) {
              continue;
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
