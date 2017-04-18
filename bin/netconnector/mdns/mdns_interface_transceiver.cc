// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/mdns_interface_transceiver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "apps/netconnector/src/mdns/dns_reading.h"
#include "apps/netconnector/src/mdns/dns_writing.h"
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

void MdnsInterfaceTransceiver::Start(const InboundMessageCallback& callback) {
  FTL_DCHECK(callback);
  FTL_DCHECK(!socket_fd_.is_valid()) << "Start called when already started.";

  FTL_LOG(INFO) << "Starting mDNS on interface " << name_ << ", "
                << (address_.is_v4() ? "IPV4" : "IPV6");

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

void MdnsInterfaceTransceiver::Stop() {
  FTL_DCHECK(socket_fd_.is_valid()) << "BeginStop called when stopped.";
  fd_waiter_.Cancel();
  socket_fd_.reset();
}

void MdnsInterfaceTransceiver::SendMessage(const DnsMessage& message,
                                           const SocketAddress& address) {
  FTL_DCHECK(address.is_valid());
  FTL_DCHECK(address.family() == address_.family());

  PacketWriter writer(std::move(outbound_buffer_));
  writer << message;
  size_t packet_size = writer.position();
  outbound_buffer_ = writer.GetPacket();

  ssize_t result =
      sendto(socket_fd_.get(), outbound_buffer_.data(), packet_size, 0,
             address.as_sockaddr(), address.socklen());
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to sendto, errno " << errno;
    return;
  }
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
    FTL_LOG(ERROR) << "Couldn't parse message from " << source_address << ", "
                   << result << " bytes.";
  }

  WaitForInbound();
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

}  // namespace mdns
}  // namespace netconnector
