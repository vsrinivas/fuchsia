// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_TRANSCEIVER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_TRANSCEIVER_H_

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/fit/function.h>
#include <netinet/in.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/mdns_interface_transceiver.h"

namespace mdns {

// Sends and receives mDNS messages on any number of interfaces.
class MdnsTransceiver : public Mdns::Transceiver {
 public:
  MdnsTransceiver();

  ~MdnsTransceiver();

  //  Mdns::Transceiver implementation.
  void Start(fuchsia::netstack::NetstackPtr netstack, const MdnsAddresses& addresses,
             fit::closure link_change_callback,
             InboundMessageCallback inbound_message_callback) override;

  void Stop() override;

  bool HasInterfaces() override;

  void SendMessage(DnsMessage* message, const ReplyAddress& reply_address) override;

  void LogTraffic() override;

  // Returns the interface transceiver with address |address| if it exists,
  // nullptr if not.
  // TODO(fxbug.dev/49875): move this back to private after multi-network test
  // is rewritten in Rust.
  MdnsInterfaceTransceiver* GetInterfaceTransceiver(const inet::IpAddress& address);

 private:
  // Handles |OnInterfaceChanged| events from |Netstack|.
  void InterfacesChanged(std::vector<fuchsia::netstack::NetInterface> interfaces);

  // Ensures that an interface transceiver exists for |address| if |address|
  // is valid. Returns true if a change was made, false otherwise.
  bool EnsureInterfaceTransceiver(
      const inet::IpAddress& address, const inet::IpAddress& alternate_address, uint32_t id,
      Media media, const std::string& name,
      std::unordered_map<inet::IpAddress, std::unique_ptr<MdnsInterfaceTransceiver>>* prev);

  // Determines if |address| identifies one of the NICs in |interface_transceivers_by_address_|.
  bool IsLocalInterfaceAddress(const inet::IpAddress& address);

  fuchsia::netstack::NetstackPtr netstack_;
  const MdnsAddresses* addresses_;
  fit::closure link_change_callback_;
  InboundMessageCallback inbound_message_callback_;
  std::string host_full_name_;
  std::unordered_map<inet::IpAddress, std::unique_ptr<MdnsInterfaceTransceiver>>
      interface_transceivers_by_address_;

 public:
  // Disallow copy, assign and move.
  MdnsTransceiver(const MdnsTransceiver&) = delete;
  MdnsTransceiver(MdnsTransceiver&&) = delete;
  MdnsTransceiver& operator=(const MdnsTransceiver&) = delete;
  MdnsTransceiver& operator=(MdnsTransceiver&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_TRANSCEIVER_H_
