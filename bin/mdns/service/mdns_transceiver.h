// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include <netinet/in.h>

#include "garnet/bin/mdns/service/interface_monitor.h"
#include "garnet/bin/mdns/service/mdns_interface_transceiver.h"
#include "lib/fxl/macros.h"

namespace mdns {

// Sends and receives mDNS messages on any number of interfaces.
class MdnsTransceiver {
 public:
  using LinkChangeCallback = std::function<void()>;
  using InboundMessageCallback =
      std::function<void(std::unique_ptr<DnsMessage>, const ReplyAddress&)>;

  MdnsTransceiver();

  ~MdnsTransceiver();

  // Enables the specified interface and family. Should be called before calling
  // |Start|. If |EnableInterface| isn't called prior to |Start|, the
  // transceiver will use all available interfaces. Otherwise it uses just the
  // interfaces that have been enabled.
  void EnableInterface(const std::string& name, sa_family_t family);

  // Starts the transceiver.
  void Start(std::unique_ptr<InterfaceMonitor> interface_monitor,
             const fxl::Closure& link_change_callback,
             const InboundMessageCallback& inbound_message_callback);

  // Stops the transceiver.
  void Stop();

  // Determines if this transceiver has interfaces.
  bool has_interfaces() { return !interfaces_.empty(); }

  // Sets the host full name. This method may be called multiple times if
  // conflicts are detected.
  void SetHostFullName(const std::string& host_full_name);

  // Sends a messaage to the specified address. A V6 interface will send to
  // |MdnsAddresses::kV6Multicast| if |reply_address.socket_address()| is
  // |MdnsAddresses::kV4Multicast|.
  void SendMessage(DnsMessage* message, const ReplyAddress& reply_address);

  // Writes log messages describing lifetime traffic.
  void LogTraffic();

 private:
  struct InterfaceId {
    InterfaceId(const std::string& name, sa_family_t family)
        : name_(name), family_(family) {}

    std::string name_;
    sa_family_t family_;
  };

  // Determines if the interface is enabled.
  bool InterfaceEnabled(const InterfaceDescriptor& interface_descr);

  // Creates a new |MdnsInterfaceTransceiver| for each interface that's ready
  // and doesn't already have one.
  void FindNewInterfaces();

  // Add an interface transceiver for the described interface at the given index
  // if it doesn't exist already. Returns true if the interface transceiver was
  // added, false if it existed already.
  bool MaybeAddInterfaceTransceiver(size_t index,
                                    const InterfaceDescriptor& interface_descr);

  // Remove the interface transceiver with the given index if it exists. Returns
  // true if the transceiver was removed, false if it didn't exist.
  bool MaybeRemoveInterfaceTransceiver(size_t index);

  std::unique_ptr<InterfaceMonitor> interface_monitor_;
  std::vector<InterfaceId> enabled_interfaces_;
  LinkChangeCallback link_change_callback_;
  InboundMessageCallback inbound_message_callback_;
  std::string host_full_name_;
  std::vector<std::unique_ptr<MdnsInterfaceTransceiver>> interfaces_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MdnsTransceiver);
};

}  // namespace mdns
