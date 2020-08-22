// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/mdns_interface_transceiver.h"

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

#include "lib/fostr/hex_dump.h"
#include "src/connectivity/network/mdns/service/dns_formatting.h"
#include "src/connectivity/network/mdns/service/dns_reading.h"
#include "src/connectivity/network/mdns/service/dns_writing.h"
#include "src/connectivity/network/mdns/service/mdns_addresses.h"
#include "src/connectivity/network/mdns/service/mdns_interface_transceiver_v4.h"
#include "src/connectivity/network/mdns/service/mdns_interface_transceiver_v6.h"
#include "src/lib/files/unique_fd.h"

namespace mdns {

// static
std::unique_ptr<MdnsInterfaceTransceiver> MdnsInterfaceTransceiver::Create(inet::IpAddress address,
                                                                           const std::string& name,
                                                                           uint32_t index,
                                                                           Media media) {
  if (address.is_v4()) {
    return std::make_unique<MdnsInterfaceTransceiverV4>(address, name, index, media);
  } else {
    return std::make_unique<MdnsInterfaceTransceiverV6>(address, name, index, media);
  }
}

MdnsInterfaceTransceiver::MdnsInterfaceTransceiver(inet::IpAddress address, const std::string& name,
                                                   uint32_t index, Media media)
    : address_(address),
      name_(name),
      index_(index),
      media_(media),
      inbound_buffer_(kMaxPacketSize),
      outbound_buffer_(kMaxPacketSize) {
  FX_DCHECK(media_ == Media::kWired || media_ == Media::kWireless);
}

MdnsInterfaceTransceiver::~MdnsInterfaceTransceiver() {}

bool MdnsInterfaceTransceiver::Start(const MdnsAddresses& addresses,
                                     InboundMessageCallback callback) {
  FX_DCHECK(callback);
  FX_DCHECK(!socket_fd_.is_valid()) << "Start called when already started.";

  addresses_ = &addresses;

  std::cout << "Starting mDNS on interface " << name_ << " " << address_ << " using port "
            << addresses.port() << "\n";

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

void MdnsInterfaceTransceiver::SetAlternateAddress(const inet::IpAddress& alternate_address) {
  FX_DCHECK(alternate_address.family() != address_.family());

  alternate_address_ = alternate_address;
}

void MdnsInterfaceTransceiver::SendMessage(DnsMessage* message,
                                           const inet::SocketAddress& address) {
  FX_DCHECK(message);
  FX_DCHECK(address.is_valid());
  FX_DCHECK(address.family() == address_.family() || address == addresses_->v4_multicast());

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
    FX_LOGS(ERROR) << "Failed to sendto " << address << " from " << name_ << " (" << address_
                   << "), size " << packet_size << ", " << strerror(errno);
    return;
  }
}

void MdnsInterfaceTransceiver::SendAddress(const std::string& host_full_name) {
  DnsMessage message;
  message.answers_.push_back(GetAddressResource(host_full_name));

  SendMessage(&message, addresses_->v4_multicast());
}

void MdnsInterfaceTransceiver::SendAddressGoodbye(const std::string& host_full_name) {
  DnsMessage message;
  // Not using |GetAddressResource| here, because we want to modify the ttl.
  message.answers_.push_back(MakeAddressResource(host_full_name, address_));
  message.answers_.back()->time_to_live_ = 0;

  SendMessage(&message, addresses_->v4_multicast());
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
  uint32_t index = this->index();
  if (if_indextoname(index, ifname) == nullptr) {
    FX_LOGS(ERROR) << "Failed to look up interface name with index=" << index << ", error "
                   << strerror(errno);
  }
  int result = setsockopt(socket_fd_.get(), SOL_SOCKET, SO_BINDTODEVICE, &ifname,
                          strnlen(ifname, IF_NAMESIZE));
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to set socket option SO_BINDTODEVICE with ifname=" << ifname
                   << ", error" << strerror(errno);
  }
  return result;
}

int MdnsInterfaceTransceiver::SetOptionSharePort() {
  int param = 1;
  int result = setsockopt(socket_fd_.get(), SOL_SOCKET, SO_REUSEADDR, &param, sizeof(param));
  if (result < 0) {
    FX_LOGS(ERROR) << "Failed to set socket option SO_REUSEADDR, " << strerror(errno);
    return result;
  }

  param = 1;
  result = setsockopt(socket_fd_.get(), SOL_SOCKET, SO_REUSEPORT, &param, sizeof(param));
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

  ReplyAddress reply_address(source_address_storage, address_, media_);

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
    inbound_buffer_.resize(result);
    // TODO(fxbug.dev/54285): Remove this message for end-users.
    FX_LOGS(WARNING) << "Couldn't parse message from " << reply_address << ", " << result
                     << " bytes: " << fostr::HexDump(inbound_buffer_);
    inbound_buffer_.resize(kMaxPacketSize);
  }

  WaitForInbound();
}

std::shared_ptr<DnsResource> MdnsInterfaceTransceiver::GetAddressResource(
    const std::string& host_full_name) {
  FX_DCHECK(address_.is_valid());

  if (!address_resource_ || address_resource_->name_.dotted_string_ != host_full_name) {
    address_resource_ = MakeAddressResource(host_full_name, address_);
  }

  return address_resource_;
}

std::shared_ptr<DnsResource> MdnsInterfaceTransceiver::GetAlternateAddressResource(
    const std::string& host_full_name) {
  FX_DCHECK(alternate_address_.is_valid());

  if (!alternate_address_resource_ ||
      alternate_address_resource_->name_.dotted_string_ != host_full_name) {
    alternate_address_resource_ = MakeAddressResource(host_full_name, alternate_address_);
  }

  return alternate_address_resource_;
}

std::shared_ptr<DnsResource> MdnsInterfaceTransceiver::MakeAddressResource(
    const std::string& host_full_name, const inet::IpAddress& address) {
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
  FX_DCHECK(resources);

  // This method is called from |SendMessage| to 'fix up' address resources in
  // a DNS message. |SendMessage| calls this method once for each of the three
  // resource lists in a message.
  //
  // If the resource list passed to this method contains no A or AAAA resources,
  // this method returns without making any modifications to the list.
  //
  // If the resource list does contain A or AAAA resources, those resources are
  // replaced with one or two new A or AAAA resources. The first of those new
  // resources contains the address bound by this instance (A if the address is
  // v4, AAAA if the address is v6). This address resource is returned by
  // |GetAddressResource|. If there is an 'alternate' address, a second new
  // address resource is added for that alternate address. That address resource
  // is returned by |GetAlternateAddressResource|.
  //
  // A/AAAA addresses in the original message come from two sources:
  // 1) The agent that sent the message may insert a placeholder A message that
  //    contains an invalid address.
  // 2) A different transceiver that sent the message previously may have
  //    inserted its own A/AAAA message(s). Because the mutated message is
  //    reused, we have to allow for this.

  std::string name;

  // Move A/AAAA resources to the end of the vector.
  auto iter = std::remove_if(
      resources->begin(), resources->end(), [&name](const std::shared_ptr<DnsResource>& resource) {
        if (resource->type_ != DnsType::kA && resource->type_ != DnsType::kAaaa) {
          return false;
        }

        name = resource->name_.dotted_string_;
        return true;
      });

  if (iter == resources->end()) {
    // No address resources found/moved.
    return;
  }

  FX_DCHECK(!name.empty());

  // There is at least one open slot. Fill it with the first A/AAAA resource
  // with the address resource for this interface.
  *iter++ = GetAddressResource(name);

  if (!alternate_address_.is_valid()) {
    // No alternate address. Clean up the remainder of the vector, and we're
    // done.
    resources->erase(iter, resources->end());
    return;
  }

  if (iter == resources->end()) {
    // We're at the end of the vector. Push the alternate address resource to
    // the back of the vector, and we're done.
    resources->push_back(GetAlternateAddressResource(name));
    return;
  }

  // Replace the second A/AAAA resource with the alternate address resource.
  *iter++ = GetAlternateAddressResource(name);

  // Clean up the remainder of the vector, and we're done.
  resources->erase(iter, resources->end());
}

}  // namespace mdns
