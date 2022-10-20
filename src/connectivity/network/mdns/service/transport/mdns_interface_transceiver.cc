// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/transport/mdns_interface_transceiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>

#include <algorithm>
#include <iostream>

#include <fbl/unique_fd.h>

#include "src/connectivity/network/mdns/service/common/formatters.h"
#include "src/connectivity/network/mdns/service/common/mdns_addresses.h"
#include "src/connectivity/network/mdns/service/encoding/dns_formatting.h"
#include "src/connectivity/network/mdns/service/encoding/dns_reading.h"
#include "src/connectivity/network/mdns/service/encoding/dns_writing.h"
#include "src/connectivity/network/mdns/service/transport/mdns_interface_transceiver_v4.h"
#include "src/connectivity/network/mdns/service/transport/mdns_interface_transceiver_v6.h"
#include "src/lib/fostr/hex_dump.h"

namespace mdns {

// static
std::unique_ptr<MdnsInterfaceTransceiver> MdnsInterfaceTransceiver::Create(inet::IpAddress address,
                                                                           const std::string& name,
                                                                           uint32_t id,
                                                                           Media media) {
  if (address.is_v4()) {
    return std::make_unique<MdnsInterfaceTransceiverV4>(address, name, id, media);
  } else {
    return std::make_unique<MdnsInterfaceTransceiverV6>(address, name, id, media);
  }
}

MdnsInterfaceTransceiver::MdnsInterfaceTransceiver(inet::IpAddress address, const std::string& name,
                                                   uint32_t id, Media media)
    : address_(address),
      name_(name),
      id_(id),
      media_(media),
      inbound_buffer_(kMaxPacketSize),
      outbound_buffer_(kMaxPacketSize) {
  FX_DCHECK(media_ == Media::kWired || media_ == Media::kWireless);
}

MdnsInterfaceTransceiver::~MdnsInterfaceTransceiver() {}

bool MdnsInterfaceTransceiver::Start(InboundMessageCallback callback) {
  FX_DCHECK(callback);
  FX_DCHECK(!socket_fd_.is_valid()) << "Start called when already started.";

  std::cout << "Starting mDNS on interface " << name_ << " using port " << MdnsAddresses::port()
            << "\n";

  socket_fd_ = fbl::unique_fd(socket(address_.family(), SOCK_DGRAM, 0));

  if (!socket_fd_.is_valid()) {
    FX_LOGS(ERROR) << "Failed to open socket, " << strerror(errno);
    return false;
  }

  // Set socket options and bind.
  if (SetOptionSharePort() != 0 || SetOptionDisableMulticastLoop() != 0 ||
      SetOptionJoinMulticastGroup() != 0 || SetOptionOutboundInterface() != 0 ||
      SetOptionUnicastTtl() != 0 || SetOptionMulticastTtl() != 0 ||
      SetOptionFamilySpecific() != 0 || SetOptionBindToDevice() != 0 || Bind() != 0) {
    socket_fd_.reset();
    return false;
  }

  inbound_message_callback_ = std::move(callback);

  WaitForInbound();
  return true;
}

void MdnsInterfaceTransceiver::Stop() {
  FX_DCHECK(socket_fd_.is_valid()) << "Stop called when stopped.";
  fd_waiter_.Cancel();
  socket_fd_.reset();
}

void MdnsInterfaceTransceiver::SetInterfaceAddresses(
    const std::vector<inet::IpAddress>& interface_addresses) {
  FX_DCHECK(!interface_addresses.empty());

  interface_addresses_ = interface_addresses;

  // These resources are a cached version of |interface_addresses_|. Make sure they get regenerated.
  interface_address_resources_.clear();
}

void MdnsInterfaceTransceiver::SendMessage(const DnsMessage& message,
                                           const inet::SocketAddress& address) {
  FX_DCHECK(address.is_valid());
  FX_DCHECK(address.family() == address_.family() || address == MdnsAddresses::v4_multicast());

  DnsMessage fixed_up_message;
  fixed_up_message.header_ = message.header_;
  fixed_up_message.questions_ = message.questions_;
  fixed_up_message.answers_ = FixUpAddresses(message.answers_);
  fixed_up_message.authorities_ = FixUpAddresses(message.authorities_);
  fixed_up_message.additionals_ = FixUpAddresses(message.additionals_);
  fixed_up_message.UpdateCounts();

  PacketWriter writer(std::move(outbound_buffer_));
  writer << fixed_up_message;
  size_t packet_size = writer.position();
  outbound_buffer_ = writer.GetPacket();

  ssize_t result = SendTo(outbound_buffer_.data(), packet_size, address);

  ++messages_sent_;
  bytes_sent_ += packet_size;

  // Host down errors are expected. See fxbug.dev/62074.
  if (result < 0 && errno != EHOSTDOWN && errno != ENETUNREACH) {
    FX_LOGS(ERROR) << "Failed to sendto " << address << " from " << name_ << " (" << address_
                   << "), size " << packet_size << ", " << strerror(errno);
  }
}

void MdnsInterfaceTransceiver::SendAddress(const std::string& host_full_name) {
  DnsMessage message;
  message.answers_.push_back(GetAddressResource(host_full_name));

  SendMessage(message, MdnsAddresses::v4_multicast());
}

void MdnsInterfaceTransceiver::SendAddressGoodbye(const std::string& host_full_name) {
  DnsMessage message;
  // Not using |GetAddressResource| here, because we want to modify the ttl.
  message.answers_.push_back(std::make_shared<DnsResource>(host_full_name, address_));
  message.answers_.back()->time_to_live_ = 0;

  SendMessage(message, MdnsAddresses::v4_multicast());
}

void MdnsInterfaceTransceiver::LogTraffic() {
  std::cout << "interface " << name_ << " " << address_ << "\n";
  std::cout << "    messages received:  " << messages_received_ << "\n";
  std::cout << "    bytes received:     " << bytes_received_ << "\n";
  std::cout << "    messages sent:      " << messages_sent_ << "\n";
  std::cout << "    bytes sent:         " << bytes_sent_ << "\n";
}

int MdnsInterfaceTransceiver::SetOptionBindToDevice() {
  char ifname[IF_NAMESIZE];
  uint32_t id = this->id();
  if (if_indextoname(id, ifname) == nullptr) {
    FX_LOGS(ERROR) << "Failed to look up interface name with index=" << id << ", error "
                   << strerror(errno);
  }
  int result = setsockopt(socket_fd_.get(), SOL_SOCKET, SO_BINDTODEVICE, &ifname,
                          static_cast<socklen_t>(strnlen(ifname, IF_NAMESIZE)));
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to set socket option SO_BINDTODEVICE with ifname=" << ifname
                   << ", error" << strerror(errno);
  }
  return result;
}

int MdnsInterfaceTransceiver::SetOptionSharePort() {
  int param = 1;
  int result = setsockopt(socket_fd_.get(), SOL_SOCKET, SO_REUSEPORT, &param, sizeof(param));
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to set socket option SO_REUSEPORT, " << strerror(errno);
  }

  return result;
}

void MdnsInterfaceTransceiver::WaitForInbound() {
  fd_waiter_.Wait([this](zx_status_t status, uint32_t events) { InboundReady(status, events); },
                  socket_fd_.get(), POLLIN);
}

void MdnsInterfaceTransceiver::InboundReady(zx_status_t status, uint32_t events) {
  sockaddr_storage source_address_storage;
  socklen_t source_address_length = address_.is_v4() ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
  ssize_t result =
      recvfrom(socket_fd_.get(), inbound_buffer_.data(), inbound_buffer_.size(), 0,
               reinterpret_cast<sockaddr*>(&source_address_storage), &source_address_length);
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to recvfrom, " << strerror(errno);
    // Wait a bit before trying again to avoid spamming the log.
    async::PostDelayedTask(
        async_get_default_dispatcher(), [this]() { WaitForInbound(); }, zx::sec(10));
    return;
  }

  ++messages_received_;
  bytes_received_ += result;

  ReplyAddress reply_address(source_address_storage, address_, id_, media_, IpVersions());

  if (reply_address.socket_address().address() == address_) {
    // This is an outgoing message that's bounced back to us. Drop it.
    WaitForInbound();
    return;
  }

  PacketReader reader(inbound_buffer_);
  reader.SetBytesRemaining(static_cast<size_t>(result));
  std::unique_ptr<DnsMessage> message = std::make_unique<DnsMessage>();
  reader >> *message.get();

  if (reader.complete()) {
    FX_DCHECK(inbound_message_callback_);
    inbound_message_callback_(std::move(message), reply_address);
  } else {
#ifdef MDNS_TRACE
    FX_LOGS(WARNING) << "Couldn't parse message from " << reply_address << ", " << result
                     << " bytes: " << fostr::HexDump(inbound_buffer_.data(), result, 0);
#else
    FX_LOGS(WARNING) << "Couldn't parse message from " << reply_address << ", " << result
                     << " bytes";
#endif  // MDNS_TRACE
  }

  WaitForInbound();
}

std::shared_ptr<DnsResource> MdnsInterfaceTransceiver::GetAddressResource(
    const std::string& host_full_name) {
  FX_DCHECK(address_.is_valid());

  if (!address_resource_ || address_resource_->name_.dotted_string_ != host_full_name) {
    address_resource_ = std::make_shared<DnsResource>(host_full_name, address_);
  }

  return address_resource_;
}

const std::vector<std::shared_ptr<DnsResource>>&
MdnsInterfaceTransceiver::GetInterfaceAddressResources(const std::string& host_full_name) {
  FX_DCHECK(!interface_addresses_.empty());

  if (interface_address_resources_.empty() ||
      interface_address_resources_[0]->name_.dotted_string_ != host_full_name) {
    interface_address_resources_.clear();
    std::transform(interface_addresses_.begin(), interface_addresses_.end(),
                   std::back_inserter(interface_address_resources_),
                   [&host_full_name](const inet::IpAddress& address) {
                     return std::make_shared<DnsResource>(host_full_name, address);
                   });
  }

  return interface_address_resources_;
}

std::vector<std::shared_ptr<DnsResource>> MdnsInterfaceTransceiver::FixUpAddresses(
    const std::vector<std::shared_ptr<DnsResource>>& resources) {
  std::string name;
  std::vector<std::shared_ptr<DnsResource>> result;
  std::copy_if(resources.begin(), resources.end(), std::back_inserter(result),
               [&name](std::shared_ptr<DnsResource> resource) {
                 switch (resource->type_) {
                   case DnsType::kA:
                     if (resource->a_.address_.address_.is_valid()) {
                       // Not a placeholder.
                       return true;
                     }
                     break;
                   case DnsType::kAaaa:
                     if (resource->aaaa_.address_.address_.is_valid()) {
                       // Not a placeholder.
                       return true;
                     }
                     break;
                   default:
                     // Not an address.
                     return true;
                 }

                 if (name.empty()) {
                   name = resource->name_.dotted_string_;
                 }

                 return false;
               });

  if (name.empty()) {
    // No placeholder address records found.
    return result;
  }

  auto& addr_resources = GetInterfaceAddressResources(name);
  std::copy(addr_resources.begin(), addr_resources.end(), std::back_inserter(result));

  return result;
}

}  // namespace mdns
