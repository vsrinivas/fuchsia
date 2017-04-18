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

  // Starts the interface transceiver.
  void Start(const InboundMessageCallback& callback);

  // Stops the interface transceiver.
  void Stop();

  // Sends a DNS message to the specified address.
  void SendMessage(const DnsMessage& message, const SocketAddress& address);

 protected:
  static constexpr int kTimeToLive_ = 255;
  static constexpr size_t kMaxPacketSize = 1500;

  MdnsInterfaceTransceiver(const netc_if_info_t& if_info, uint32_t index);

  virtual int SetOptionJoinMulticastGroup() = 0;
  virtual int SetOptionOutboundInterface() = 0;
  virtual int SetOptionUnicastTtl() = 0;
  virtual int SetOptionMulticastTtl() = 0;
  virtual int SetOptionFamilySpecific() = 0;
  virtual int Bind() = 0;

  IpAddress address_;
  uint32_t index_;
  ftl::UniqueFD socket_fd_;

 private:
  int SetOptionSharePort();

  void WaitForInbound();

  void InboundReady(mx_status_t status, uint32_t events);

  std::string name_;
  mtl::FDWaiter fd_waiter_;
  std::vector<uint8_t> inbound_buffer_;
  std::vector<uint8_t> outbound_buffer_;
  InboundMessageCallback inbound_message_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MdnsInterfaceTransceiver);
};

}  // namespace mdns
}  // namespace netconnector
