// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_interface_transceiver.h"

#include <iostream>

#include <arpa/inet.h>
#include <errno.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <poll.h>
#include <sys/socket.h>

#include "garnet/bin/mdns/service/dns_formatting.h"
#include "garnet/bin/mdns/service/dns_reading.h"
#include "garnet/bin/mdns/service/dns_writing.h"
#include "garnet/bin/mdns/service/mdns_addresses.h"
#include "garnet/bin/mdns/service/mdns_interface_transceiver_v4.h"
#include "garnet/bin/mdns/service/mdns_interface_transceiver_v6.h"
#include "lib/fostr/hex_dump.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

namespace mdns {

// static
std::unique_ptr<MdnsInterfaceTransceiver> MdnsInterfaceTransceiver::Create(
    IpAddress address, const std::string& name, uint32_t index) {
  MdnsInterfaceTransceiver* interface_transceiver;

  if (address.is_v4()) {
    interface_transceiver =
        new MdnsInterfaceTransceiverV4(address, name, index);
  } else {
    interface_transceiver =
        new MdnsInterfaceTransceiverV6(address, name, index);
  }

  return std::unique_ptr<MdnsInterfaceTransceiver>(interface_transceiver);
}

MdnsInterfaceTransceiver::MdnsInterfaceTransceiver(IpAddress address,
                                                   const std::string& name,
                                                   uint32_t index)
    : address_(address),
      name_(name),
      index_(index),
      inbound_buffer_(kMaxPacketSize),
      outbound_buffer_(kMaxPacketSize) {}

MdnsInterfaceTransceiver::~MdnsInterfaceTransceiver() {}

bool MdnsInterfaceTransceiver::Start(InboundMessageCallback callback) {
  FXL_DCHECK(callback);
  FXL_DCHECK(!socket_fd_.is_valid()) << "Start called when already started.";

  std::cerr << "Starting mDNS on interface " << name_ << ", IPv4 "
            << address_ << "\n";

  socket_fd_ = fxl::UniqueFD(socket(address_.family(), SOCK_DGRAM, 0));

  if (!socket_fd_.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open socket, errno " << errno;
    return false;
  }

  // Set socket options and bind.
  if (SetOptionSharePort() != 0 || SetOptionJoinMulticastGroup() != 0 ||
      SetOptionOutboundInterface() != 0 || SetOptionUnicastTtl() != 0 ||
      SetOptionMulticastTtl() != 0 || SetOptionFamilySpecific() != 0 ||
      Bind() != 0) {
    socket_fd_.reset();
    return false;
  }

  inbound_message_callback_ = std::move(callback);

  WaitForInbound();
  return true;
}

void MdnsInterfaceTransceiver::Stop() {
  FXL_DCHECK(socket_fd_.is_valid()) << "Stop called when stopped.";
  fd_waiter_.Cancel();
  socket_fd_.reset();
}

void MdnsInterfaceTransceiver::SetAlternateAddress(
    const IpAddress& alternate_address) {
  FXL_DCHECK(alternate_address.family() != address_.family());

  alternate_address_ = alternate_address;
}

void MdnsInterfaceTransceiver::SendMessage(DnsMessage* message,
                                           const SocketAddress& address) {
  FXL_DCHECK(message);
  FXL_DCHECK(address.is_valid());
  FXL_DCHECK(address.family() == address_.family() ||
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

  ++messages_sent_;
  bytes_sent_ += packet_size;

  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to sendto, errno " << errno;
    return;
  }
}

void MdnsInterfaceTransceiver::SendAddress(const std::string& host_full_name) {
  DnsMessage message;
  message.answers_.push_back(GetAddressResource(host_full_name));

  SendMessage(&message, MdnsAddresses::kV4Multicast);
}

void MdnsInterfaceTransceiver::SendAddressGoodbye(
    const std::string& host_full_name) {
  DnsMessage message;
  // Not using |GetAddressResource| here, because we want to modify the ttl.
  message.answers_.push_back(MakeAddressResource(host_full_name, address_));
  message.answers_.back()->time_to_live_ = 0;

  SendMessage(&message, MdnsAddresses::kV4Multicast);
}

void MdnsInterfaceTransceiver::LogTraffic() {
  std::cout << "interface " << name_ << " " << address_ << "\n";
  std::cout << "    messages received:  " << messages_received_ << "\n";
  std::cout << "    bytes received:     " << bytes_received_ << "\n";
  std::cout << "    messages sent:      " << messages_sent_ << "\n";
  std::cout << "    bytes sent:         " << bytes_sent_ << "\n";
}

int MdnsInterfaceTransceiver::SetOptionSharePort() {
  int param = 1;
  int result = setsockopt(socket_fd_.get(), SOL_SOCKET, SO_REUSEADDR, &param,
                          sizeof(param));
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to set socket option SO_REUSEADDR, errno "
                   << errno;
  }

  return result;
}

void MdnsInterfaceTransceiver::WaitForInbound() {
  fd_waiter_.Wait([this](zx_status_t status,
                         uint32_t events) { InboundReady(status, events); },
                  socket_fd_.get(), POLLIN);
}

void MdnsInterfaceTransceiver::InboundReady(zx_status_t status,
                                            uint32_t events) {
  sockaddr_storage source_address_storage;
  socklen_t source_address_length =
      address_.is_v4() ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
  ssize_t result =
      recvfrom(socket_fd_.get(), inbound_buffer_.data(), inbound_buffer_.size(),
               0, reinterpret_cast<sockaddr*>(&source_address_storage),
               &source_address_length);
  if (result < 0) {
    FXL_LOG(ERROR) << "Failed to recvfrom, errno " << errno;
    // Wait a bit before trying again to avoid spamming the log.
    async::PostDelayedTask(async_get_default_dispatcher(), [this]() { WaitForInbound(); },
                           zx::sec(10));
    return;
  }

  ++messages_received_;
  bytes_received_ += result;

  ReplyAddress reply_address(source_address_storage, index_);

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
    FXL_DCHECK(inbound_message_callback_);
    inbound_message_callback_(std::move(message), reply_address);
  } else {
    inbound_buffer_.resize(result);
    FXL_LOG(ERROR) << "Couldn't parse message from " << reply_address << ", "
                   << result << " bytes: " << fostr::HexDump(inbound_buffer_);
    inbound_buffer_.resize(kMaxPacketSize);
  }

  WaitForInbound();
}

std::shared_ptr<DnsResource> MdnsInterfaceTransceiver::GetAddressResource(
    const std::string& host_full_name) {
  FXL_DCHECK(address_.is_valid());

  if (!address_resource_ ||
      address_resource_->name_.dotted_string_ != host_full_name) {
    address_resource_ = MakeAddressResource(host_full_name, address_);
  }

  return address_resource_;
}

std::shared_ptr<DnsResource>
MdnsInterfaceTransceiver::GetAlternateAddressResource(
    const std::string& host_full_name) {
  FXL_DCHECK(alternate_address_.is_valid());

  if (!alternate_address_resource_ ||
      alternate_address_resource_->name_.dotted_string_ != host_full_name) {
    alternate_address_resource_ =
        MakeAddressResource(host_full_name, alternate_address_);
  }

  return alternate_address_resource_;
}

std::shared_ptr<DnsResource> MdnsInterfaceTransceiver::MakeAddressResource(
    const std::string& host_full_name, const IpAddress& address) {
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
    // Agents shouldn't produce AAAA resources, just A resource placeholders.
    FXL_DCHECK((*iter)->type_ != DnsType::kAaaa);

    if ((*iter)->type_ == DnsType::kA) {
      auto name = (*iter)->name_.dotted_string_;
      *iter = GetAddressResource(name);

      if (alternate_address_.is_valid()) {
        // Insert the alternate address record after the first one.
        iter = resources->insert(++iter, GetAlternateAddressResource(name));
      }
    }
  }
}

}  // namespace mdns
