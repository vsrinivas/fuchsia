// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "apps/netconnector/src/ip_address.h"
#include "apps/netconnector/src/mdns/dns_message.h"
#include "apps/netconnector/src/socket_address.h"
#include "apps/netstack/apps/include/netconfig.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/fd_waiter.h"

namespace netconnector {
namespace mdns {

// Handles mDNS communication for a single NIC. This class is abstract and has
// two concrete implementations providing family-specific behavior:
// |MdnsInterfaceTransceiverV4| and |MdnsInterfaceTransceiverV6|.
class MdnsInterfaceTransceiver {
 public:
  // Callback to deliver inbound messages with source address and interface
  // index.
  using InboundMessageCallback = std::function<
      void(std::unique_ptr<DnsMessage>, const SocketAddress&, uint32_t)>;

  // Creates the variant of |MdnsInterfaceTransceiver| appropriate for the
  // address family specified in |if_info|. |index| is the index of the
  // interface.
  static std::unique_ptr<MdnsInterfaceTransceiver> Create(
      const netc_if_info_t& if_info,
      uint32_t index);

  virtual ~MdnsInterfaceTransceiver();

  const std::string& name() const { return name_; }

  const IpAddress& address() const { return address_; }

  // Sets an alternate address for the interface.
  void SetAlternateAddress(const std::string& host_full_name,
                           const IpAddress& alternate_address);

  // Starts the interface transceiver.
  void Start(const std::string& host_full_name,
             const InboundMessageCallback& callback);

  // Stops the interface transceiver.
  void Stop();

  // Sends a messaage to the specified address. A V6 interface will send to
  // |MdnsAddresses::kV6Multicast| if |dest_address| is
  // |MdnsAddresses::kV4Multicast|. This method expects there to be at most two
  // address records per record vector and, if there are two, that they are
  // adjacent. The same constraints will apply when this method returns.
  void SendMessage(DnsMessage* message, const SocketAddress& address);

 protected:
  static constexpr int kTimeToLive_ = 255;
  static constexpr size_t kMaxPacketSize = 1500;

  MdnsInterfaceTransceiver(const netc_if_info_t& if_info, uint32_t index);

  uint32_t index() const { return index_; }

  const ftl::UniqueFD& socket_fd() const { return socket_fd_; }

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

  void InboundReady(mx_status_t status, uint32_t events);

  std::shared_ptr<DnsResource> MakeAddressResource(
      const std::string& host_full_name,
      const IpAddress& address);

  // Fixes up the address records in the vector. This method expects there to
  // be at most two address records in the vector and, if there are two, that
  // they are adjacent. The same constraints will apply when this method
  // returns.
  void FixUpAddresses(std::vector<std::shared_ptr<DnsResource>>* resources);

  IpAddress address_;
  uint32_t index_;
  std::string name_;
  ftl::UniqueFD socket_fd_;
  mtl::FDWaiter fd_waiter_;
  std::vector<uint8_t> inbound_buffer_;
  std::vector<uint8_t> outbound_buffer_;
  InboundMessageCallback inbound_message_callback_;
  std::shared_ptr<DnsResource> address_resource_;
  std::shared_ptr<DnsResource> alternate_address_resource_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MdnsInterfaceTransceiver);
};

}  // namespace mdns
}  // namespace netconnector
