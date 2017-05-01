// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include <netinet/in.h>

#include "apps/netconnector/src/mdns/mdns_interface_transceiver.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"

namespace netconnector {
namespace mdns {

// Sends and receives mDNS messages on any number of interfaces.
class MdnsTransceiver {
 public:
  using InboundMessageCallback = std::function<
      void(std::unique_ptr<DnsMessage>, const SocketAddress&, uint32_t)>;

  MdnsTransceiver();

  ~MdnsTransceiver();

  // Enables the specified interface and family. Should be called before calling
  // |Start|. If |EnableInterface| isn't called prior to |Start|, the
  // transceiver will use all available interfaces. Otherwise it uses just the
  // interfaces that have been enabled.
  void EnableInterface(const std::string& name, sa_family_t family);

  // Starts the transceiver. Returns true if successful.
  bool Start(const std::string& host_full_name,
             const InboundMessageCallback& inbound_message_callback);

  // Stops the transceiver.
  void Stop();

  // Sends a messaage to the specified address on the specified interface. A
  // V6 interface will send to |MdnsAddresses::kV6Multicast| if |dest_address|
  // is |MdnsAddresses::kV4Multicast|.
  void SendMessage(DnsMessage* message,
                   const SocketAddress& dest_address,
                   uint32_t interface_index);

 private:
  static const ftl::TimeDelta kMinAddressRecheckDelay;
  static const ftl::TimeDelta kMaxAddressRecheckDelay;
  static constexpr int64_t kAddressRecheckDelayMultiplier = 2;

  struct InterfaceId {
    InterfaceId(const std::string& name, sa_family_t family)
        : name_(name), family_(family) {}

    std::string name_;
    sa_family_t family_;
  };

  // Determines if the interface is enabled.
  bool InterfaceEnabled(netc_if_info_t* if_info);

  // Creates a new |MdnsInterfaceTransciver| for each interface that's ready
  // and doesn't already have one. Schedules another call to this method if
  // unready interfaces were found.
  bool FindNewInterfaces();

  // Determines if a |MdnsInterfaceTransciver| has already been created for the
  // specified address.
  bool InterfaceAlreadyFound(const IpAddress& address);

  // Determines if |address| has been set (e.g. via DHCP).
  bool AddressIsSet(const IpAddress& address);

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::vector<InterfaceId> enabled_interfaces_;
  InboundMessageCallback inbound_message_callback_;
  std::string host_full_name_;
  std::vector<std::unique_ptr<MdnsInterfaceTransceiver>> interfaces_;
  ftl::TimeDelta address_recheck_delay_ = kMinAddressRecheckDelay;

  FTL_DISALLOW_COPY_AND_ASSIGN(MdnsTransceiver);
};

}  // namespace mdns
}  // namespace netconnector
