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
  bool has_interfaces() { return !interface_transceivers_.empty(); }

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

  // Returns the interface transceiver at |index| if it exists, nullptr if not.
  MdnsInterfaceTransceiver* GetInterfaceTransceiver(size_t index);

  // Sets the interface transceiver at |index|. |interface_transceiver| may be
  // null.
  void SetInterfaceTransceiver(
      size_t index,
      std::unique_ptr<MdnsInterfaceTransceiver> interface_transceiver);

  // Determines if the interface is enabled.
  bool InterfaceEnabled(const InterfaceDescriptor& interface_descr);

  // Ensures that the collection of |MdnsInterfaceTransceiver|s is up-to-date
  // with respect to the current set of interfaces.
  void OnLinkChange();

  // Adds an interface transceiver for the described interface at the given
  // index. The interface transceiver must not exist already.
  void AddInterfaceTransceiver(size_t index,
                               const InterfaceDescriptor& interface_descr);

  // Replace an interface transceiver for the described interface at the given
  // index. The interface transceiver must exist.
  void ReplaceInterfaceTransceiver(size_t index,
                                   const InterfaceDescriptor& interface_descr);

  // Remove the interface transceiver with the given index. The interface
  // transceiver must exist.
  void RemoveInterfaceTransceiver(size_t index);

  std::unique_ptr<InterfaceMonitor> interface_monitor_;
  std::vector<InterfaceId> enabled_interfaces_;
  LinkChangeCallback link_change_callback_;
  InboundMessageCallback inbound_message_callback_;
  std::string host_full_name_;
  std::vector<std::unique_ptr<MdnsInterfaceTransceiver>>
      interface_transceivers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MdnsTransceiver);
};

}  // namespace mdns
