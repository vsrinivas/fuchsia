// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TRANSPORT_MDNS_TRANSCEIVER_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TRANSPORT_MDNS_TRANSCEIVER_H_

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <lib/fit/function.h>
#include <netinet/in.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "src/connectivity/network/lib/net_interfaces/cpp/net_interfaces.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/connectivity/network/mdns/service/transport/mdns_interface_transceiver.h"

namespace mdns {

// Sends and receives mDNS messages on any number of interfaces.
class MdnsTransceiver : public Mdns::Transceiver {
 public:
  MdnsTransceiver();

  ~MdnsTransceiver();

  //  Mdns::Transceiver implementation.
  void Start(fuchsia::net::interfaces::WatcherPtr watcher, fit::closure link_change_callback,
             InboundMessageCallback inbound_message_callback,
             InterfaceTransceiverCreateFunction transceiver_factory) override;

  void Stop() override;

  bool HasInterfaces() override;

  void SendMessage(const DnsMessage& message, const ReplyAddress& reply_address) override;

  void LogTraffic() override;

  std::vector<HostAddress> LocalHostAddresses() override;

  // Returns the interface transceiver with address |address| if it exists,
  // nullptr if not.
  // TODO(fxbug.dev/49875): move this back to private after multi-network test
  // is rewritten in Rust.
  MdnsInterfaceTransceiver* GetInterfaceTransceiver(const inet::IpAddress& address);

 private:
  // Starts an interface transceiver for every address in |properties|.
  // Returns true iff at least one interface transceiver was started.
  bool StartInterfaceTransceivers(const net::interfaces::Properties& properties);

  // Stops the interface transceiver on |address| if it exists.
  // Returns true iff at least one interface transceiver was stopped.
  bool StopInterfaceTransceiver(const inet::IpAddress& address);

  // Handles a fuchsia.net.interfaces |Added| or |Existing| event (indicated by |event_type|) by
  // starting interface transceivers if |discovered| indicates that the interface is online and not
  // loopback.  Returns true iff at least one interface transceiver was started.
  bool OnInterfaceDiscovered(fuchsia::net::interfaces::Properties discovered,
                             const char* event_type);

  // Handles a |fuchsia::net::interfaces::Event|.
  void OnInterfacesEvent(fuchsia::net::interfaces::Event event);

  // Ensures that an interface transceiver exists for |address| if |address|
  // is valid. Returns true if a change was made, false otherwise.
  bool EnsureInterfaceTransceiver(const inet::IpAddress& address,
                                  const std::vector<inet::IpAddress>& interface_addresses,
                                  uint32_t id, Media media, const std::string& name);

  // Determines if |address| identifies one of the NICs in |interface_transceivers_by_address_|.
  bool IsLocalInterfaceAddress(const inet::IpAddress& address);

  fuchsia::net::interfaces::WatcherPtr interface_watcher_;
  fit::closure link_change_callback_;
  InboundMessageCallback inbound_message_callback_;
  InterfaceTransceiverCreateFunction transceiver_factory_;
  std::string local_host_full_name_;
  std::unordered_map<inet::IpAddress, std::unique_ptr<MdnsInterfaceTransceiver>>
      interface_transceivers_by_address_;
  std::unordered_map<uint64_t, net::interfaces::Properties> interface_properties_;

 public:
  // Disallow copy, assign and move.
  MdnsTransceiver(const MdnsTransceiver&) = delete;
  MdnsTransceiver(MdnsTransceiver&&) = delete;
  MdnsTransceiver& operator=(const MdnsTransceiver&) = delete;
  MdnsTransceiver& operator=(MdnsTransceiver&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_TRANSPORT_MDNS_TRANSCEIVER_H_
