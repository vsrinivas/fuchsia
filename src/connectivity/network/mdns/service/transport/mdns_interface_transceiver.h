// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TRANSPORT_MDNS_INTERFACE_TRANSCEIVER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TRANSPORT_MDNS_INTERFACE_TRANSCEIVER_H_

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/connectivity/network/mdns/service/common/mdns_addresses.h"
#include "src/connectivity/network/mdns/service/common/reply_address.h"
#include "src/connectivity/network/mdns/service/encoding/dns_message.h"
#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/lib/inet/ip_address.h"
#include "src/lib/inet/socket_address.h"

namespace mdns {

// Handles mDNS communication for a single NIC. This class is abstract and has
// two concrete implementations providing family-specific behavior:
// |MdnsInterfaceTransceiverV4| and |MdnsInterfaceTransceiverV6|.
class MdnsInterfaceTransceiver {
 public:
  // Callback to deliver inbound messages with reply address.
  using InboundMessageCallback =
      fit::function<void(std::unique_ptr<DnsMessage>, const ReplyAddress&)>;

  // Creates the variant of |MdnsInterfaceTransceiver| appropriate for the
  // address family specified in |address|.
  static std::unique_ptr<MdnsInterfaceTransceiver> Create(inet::IpAddress address,
                                                          const std::string& name, uint32_t id,
                                                          Media media);

  virtual ~MdnsInterfaceTransceiver();

  const inet::IpAddress& address() const { return address_; }

  const std::string& name() const { return name_; }

  uint32_t id() const { return id_; }

  Media media() const { return media_; }

  // Starts the interface transceiver.
  virtual bool Start(InboundMessageCallback callback);

  // Stops the interface transceiver.
  virtual void Stop();

  // Sets the list of all addresses for the interface.
  void SetInterfaceAddresses(const std::vector<inet::IpAddress>& interface_addresses);

  // Sends a message to the specified address. A V6 interface will send to
  // |MdnsAddresses::V6Multicast| if |address| is |MdnsAddresses::V4Multicast|. If any resource
  // section of the message contains one or more address placeholders, those placeholders will be
  // replaced by address resource records for all this interface's addresses.
  void SendMessage(const DnsMessage& message, const inet::SocketAddress& address);

  // Sends a message containing only an address resource for this interface.
  void SendAddress(const std::string& host_full_name);

  // Sends a message containing only an address resource for this interface with
  // zero ttl, indicating that the address is no longer valid.
  void SendAddressGoodbye(const std::string& host_full_name);

  // Writes log messages describing lifetime traffic.
  void LogTraffic();

  virtual IpVersions IpVersions() = 0;

 protected:
  static constexpr int kTimeToLive_ = 255;
  // RFC6762 suggests a max packet size of 1500, but we see bigger packets in the wild. 9000 is
  // the maximum size for 'jumbo' packets.
  static constexpr size_t kMaxPacketSize = 9000;

  MdnsInterfaceTransceiver(inet::IpAddress address, const std::string& name, uint32_t id,
                           Media media);

  const fbl::unique_fd& socket_fd() const { return socket_fd_; }

  virtual int SetOptionDisableMulticastLoop() = 0;
  virtual int SetOptionJoinMulticastGroup() = 0;
  virtual int SetOptionOutboundInterface() = 0;
  virtual int SetOptionUnicastTtl() = 0;
  virtual int SetOptionMulticastTtl() = 0;
  virtual int SetOptionFamilySpecific() = 0;
  virtual int Bind() = 0;
  virtual ssize_t SendTo(const void* buffer, size_t size, const inet::SocketAddress& address) = 0;

 private:
  int SetOptionSharePort();
  int SetOptionBindToDevice();

  void WaitForInbound();

  void InboundReady(zx_status_t status, uint32_t events);

  // Returns an address resource (A/AAAA) record with the given name and the
  // address contained in |address_|, which must be valid.
  std::shared_ptr<DnsResource> GetAddressResource(const std::string& host_full_name);

  // Returns address resource (A/AAAA) records with the given name and the
  // addresses contained in |interface_addresses_|.
  const std::vector<std::shared_ptr<DnsResource>>& GetInterfaceAddressResources(
      const std::string& host_full_name);

  // Returns a new vector with the same resources as |resources| except that any placeholder address
  // records have been replaced by address records for |interface_addresses_|.
  std::vector<std::shared_ptr<DnsResource>> FixUpAddresses(
      const std::vector<std::shared_ptr<DnsResource>>& resources);

  inet::IpAddress address_;
  std::vector<inet::IpAddress> interface_addresses_;
  std::string name_;
  uint32_t id_;
  Media media_;
  fbl::unique_fd socket_fd_;
  fsl::FDWaiter fd_waiter_;
  std::vector<uint8_t> inbound_buffer_;
  std::vector<uint8_t> outbound_buffer_;
  InboundMessageCallback inbound_message_callback_;
  std::shared_ptr<DnsResource> address_resource_;
  std::vector<std::shared_ptr<DnsResource>> interface_address_resources_;
  uint64_t messages_received_ = 0;
  uint64_t bytes_received_ = 0;
  uint64_t messages_sent_ = 0;
  uint64_t bytes_sent_ = 0;

 public:
  // Disallow copy, assign and move.
  MdnsInterfaceTransceiver(const MdnsInterfaceTransceiver&) = delete;
  MdnsInterfaceTransceiver(MdnsInterfaceTransceiver&&) = delete;
  MdnsInterfaceTransceiver& operator=(const MdnsInterfaceTransceiver&) = delete;
  MdnsInterfaceTransceiver& operator=(MdnsInterfaceTransceiver&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TRANSPORT_MDNS_INTERFACE_TRANSCEIVER_H_
