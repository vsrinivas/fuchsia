// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "garnet/bin/netconnector/ip_address.h"
#include "garnet/bin/netconnector/mdns/dns_message.h"
#include "garnet/bin/netconnector/mdns/reply_address.h"
#include "garnet/bin/netconnector/socket_address.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"

namespace netstack {
class NetInterface;
}  // namespace netstack

namespace netconnector {
namespace mdns {

// Handles mDNS communication for a single NIC. This class is abstract and has
// two concrete implementations providing family-specific behavior:
// |MdnsInterfaceTransceiverV4| and |MdnsInterfaceTransceiverV6|.
class MdnsInterfaceTransceiver {
 public:
  // Callback to deliver inbound messages with reply address.
  using InboundMessageCallback =
      std::function<void(std::unique_ptr<DnsMessage>, const ReplyAddress&)>;

  // Creates the variant of |MdnsInterfaceTransceiver| appropriate for the
  // address family specified in |if_info|. |index| is the index of the
  // interface.
  static std::unique_ptr<MdnsInterfaceTransceiver> Create(
      const netstack::NetInterface* if_info,
      uint32_t index);

  virtual ~MdnsInterfaceTransceiver();

  const std::string& name() const { return name_; }

  const IpAddress& address() const { return address_; }

  // Starts the interface transceiver.
  bool Start(const InboundMessageCallback& callback);

  // Stops the interface transceiver.
  void Stop();

  // Sets the host full name. This method may be called multiple times if
  // conflicts are detected.
  void SetHostFullName(const std::string& host_full_name);

  // Sets an alternate address for the interface. |host_full_name| may be empty,
  // in which case |SetHostFullName| will be called later.
  void SetAlternateAddress(const std::string& host_full_name,
                           const IpAddress& alternate_address);

  // Sends a messaage to the specified address. A V6 interface will send to
  // |MdnsAddresses::kV6Multicast| if |reply_address| is
  // |MdnsAddresses::kV4Multicast|. This method expects there to be at most two
  // address records per record vector and, if there are two, that they are
  // adjacent. The same constraints will apply when this method returns.
  void SendMessage(DnsMessage* message, const SocketAddress& address);

 protected:
  static constexpr int kTimeToLive_ = 255;
  static constexpr size_t kMaxPacketSize = 1500;

  MdnsInterfaceTransceiver(const netstack::NetInterface* if_info,
                           uint32_t index);

  uint32_t index() const { return index_; }

  const fxl::UniqueFD& socket_fd() const { return socket_fd_; }

  virtual int SetOptionJoinMulticastGroup() = 0;
  virtual int SetOptionOutboundInterface() = 0;
  virtual int SetOptionUnicastTtl() = 0;
  virtual int SetOptionMulticastTtl() = 0;
  virtual int SetOptionFamilySpecific() = 0;
  virtual int Bind() = 0;
  virtual int SendTo(const void* buffer,
                     size_t size,
                     const SocketAddress& address) = 0;

 private:
  int SetOptionSharePort();

  void WaitForInbound();

  void InboundReady(zx_status_t status, uint32_t events);

  std::shared_ptr<DnsResource> MakeAddressResource(
      const std::string& host_full_name,
      const IpAddress& address);

  // Fixes up the address records in the vector. This method expects there to
  // be at most two address records in the vector and, if there are two, that
  // they are adjacent. The same constraints will apply when this method
  // returns.
  void FixUpAddresses(std::vector<std::shared_ptr<DnsResource>>* resources);

  IpAddress address_;
  IpAddress alternate_address_;
  uint32_t index_;
  std::string name_;
  fxl::UniqueFD socket_fd_;
  fsl::FDWaiter fd_waiter_;
  std::vector<uint8_t> inbound_buffer_;
  std::vector<uint8_t> outbound_buffer_;
  InboundMessageCallback inbound_message_callback_;
  std::shared_ptr<DnsResource> address_resource_;
  std::shared_ptr<DnsResource> alternate_address_resource_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MdnsInterfaceTransceiver);
};

}  // namespace mdns
}  // namespace netconnector
