// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/mdns_interface_transceiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "apps/netconnector/src/mdns/dns_formatting.h"
#include "apps/netconnector/src/mdns/dns_reading.h"
#include "apps/netconnector/src/mdns/dns_writing.h"
#include "apps/netconnector/src/mdns/mdns_addresses.h"
#include "apps/netconnector/src/mdns/mdns_interface_transceiver_v4.h"
#include "apps/netconnector/src/mdns/mdns_interface_transceiver_v6.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace netconnector {
namespace mdns {

// static
std::unique_ptr<MdnsInterfaceTransceiver> MdnsInterfaceTransceiver::Create(
    const netc_if_info_t& if_info,
    uint32_t index) {
  MdnsInterfaceTransceiver* interface_transceiver;

  if (if_info.addr.ss_family == AF_INET) {
    interface_transceiver = new MdnsInterfaceTransceiverV4(if_info, index);
  } else {
    interface_transceiver = new MdnsInterfaceTransceiverV6(if_info, index);
  }

  return std::unique_ptr<MdnsInterfaceTransceiver>(interface_transceiver);
}

MdnsInterfaceTransceiver::MdnsInterfaceTransceiver(
    const netc_if_info_t& if_info,
    uint32_t index)
    : address_((struct sockaddr*)&if_info.addr),
      index_(index),
      name_(if_info.name),
      inbound_buffer_(kMaxPacketSize),
      outbound_buffer_(kMaxPacketSize) {}

MdnsInterfaceTransceiver::~MdnsInterfaceTransceiver() {}

void MdnsInterfaceTransceiver::Start(const std::string& host_full_name,
                                     const InboundMessageCallback& callback) {
  FTL_DCHECK(callback);
  FTL_DCHECK(!socket_fd_.is_valid()) << "Start called when already started.";

  FTL_LOG(INFO) << "Starting mDNS on interface " << name_ << ", address "
                << address_;

  address_resource_ = MakeAddressResource(host_full_name, address_);

  socket_fd_ = ftl::UniqueFD(socket(address_.family(), SOCK_DGRAM, 0));

  if (!socket_fd_.is_valid()) {
    FTL_LOG(ERROR) << "Failed to open socket, errno " << errno;
    return;
  }

  // Set socket options and bind.
  if (SetOptionSharePort() != 0 || SetOptionJoinMulticastGroup() != 0 ||
      SetOptionOutboundInterface() != 0 || SetOptionUnicastTtl() != 0 ||
      SetOptionMulticastTtl() != 0 || SetOptionFamilySpecific() != 0 ||
      Bind() != 0) {
    socket_fd_.reset();
    return;
  }

  inbound_message_callback_ = callback;

  WaitForInbound();
}

void MdnsInterfaceTransceiver::SetAlternateAddress(
    const std::string& host_full_name,
    const IpAddress& alternate_address) {
  FTL_DCHECK(!alternate_address_resource_);
  FTL_DCHECK(alternate_address.family() != address_.family());

  alternate_address_resource_ =
      MakeAddressResource(host_full_name, alternate_address);
}

void MdnsInterfaceTransceiver::Stop() {
  FTL_DCHECK(socket_fd_.is_valid()) << "BeginStop called when stopped.";
  fd_waiter_.Cancel();
  socket_fd_.reset();
}

void MdnsInterfaceTransceiver::SendMessage(DnsMessage* message,
                                           const SocketAddress& address) {
  FTL_DCHECK(message);
  FTL_DCHECK(address.is_valid());
  FTL_DCHECK(address.family() == address_.family() ||
             address == MdnsAddresses::kV4Multicast);

  FixUpAddresses(&message->answers_);
  FixUpAddresses(&message->authorities_);
  FixUpAddresses(&message->additionals_);
  message->UpdateCounts();

  PacketWriter writer(std::move(outbound_buffer_));
  writer << *message;
  size_t packet_size = writer.position();
  outbound_buffer_ = writer.GetPacket();

  ssize_t result = SendTo(outbound_buffer_.data(), packet_size, address);

  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to sendto, errno " << errno;
    return;
  }
}

int MdnsInterfaceTransceiver::SetOptionSharePort() {
  int param = 1;
  int result = setsockopt(socket_fd_.get(), SOL_SOCKET, SO_REUSEADDR, &param,
                          sizeof(param));
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to set socket option SO_REUSEADDR, errno "
                   << errno;
  }

  return result;
}

void MdnsInterfaceTransceiver::WaitForInbound() {
  fd_waiter_.Wait([this](mx_status_t status,
                         uint32_t events) { InboundReady(status, events); },
                  socket_fd_.get(), EPOLLIN);
}

void MdnsInterfaceTransceiver::InboundReady(mx_status_t status,
                                            uint32_t events) {
  sockaddr_storage source_address_storage;
  socklen_t source_address_length =
      address_.is_v4() ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
  ssize_t result = recvfrom(
      socket_fd_.get(), inbound_buffer_.data(), inbound_buffer_.size(),
      MSG_WAITALL, reinterpret_cast<sockaddr*>(&source_address_storage),
      &source_address_length);
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to recvfrom, errno " << errno;
    WaitForInbound();
    return;
  }

  SocketAddress source_address(source_address_storage);

  PacketReader reader(inbound_buffer_);
  reader.SetBytesRemaining(static_cast<size_t>(result));
  std::unique_ptr<DnsMessage> message = std::make_unique<DnsMessage>();
  reader >> *message.get();

  if (reader.complete()) {
    FTL_DCHECK(inbound_message_callback_);
    inbound_message_callback_(std::move(message), source_address, index_);
  } else {
    inbound_buffer_.resize(result);
    FTL_LOG(ERROR) << "Couldn't parse message from " << source_address << ", "
                   << result << " bytes: " << inbound_buffer_;
    inbound_buffer_.resize(kMaxPacketSize);
  }

  WaitForInbound();
}

std::shared_ptr<DnsResource> MdnsInterfaceTransceiver::MakeAddressResource(
    const std::string& host_full_name,
    const IpAddress& address) {
  std::shared_ptr<DnsResource> resource;

  if (address.is_v4()) {
    resource = std::make_shared<DnsResource>(host_full_name, DnsType::kA);
    resource->a_.address_.address_ = address;
  } else {
    resource = std::make_shared<DnsResource>(host_full_name, DnsType::kAaaa);
    resource->aaaa_.address_.address_ = address;
  }

  return resource;
}

void MdnsInterfaceTransceiver::FixUpAddresses(
    std::vector<std::shared_ptr<DnsResource>>* resources) {
  for (auto iter = resources->begin(); iter != resources->end(); ++iter) {
    if ((*iter)->type_ == DnsType::kA || (*iter)->type_ == DnsType::kAaaa) {
      **iter = *address_resource_;

      auto next_iter = iter;
      ++next_iter;

      bool next_is_address = next_iter != resources->end() &&
                             ((*next_iter)->type_ == DnsType::kA ||
                              (*next_iter)->type_ == DnsType::kAaaa);

      if (alternate_address_resource_) {
        if (next_is_address) {
          // There's already a second address record. Copy the alternate address
          // record over it.
          **next_iter = *alternate_address_resource_;
        } else {
          // There's no second address record. Insert the alternate address
          // record after the first one.
          resources->insert(next_iter, alternate_address_resource_);
        }
      } else if (next_is_address) {
        // We don't need this second address record.
        resources->erase(next_iter);
      }

      break;
    }
  }
}

}  // namespace mdns
}  // namespace netconnector
