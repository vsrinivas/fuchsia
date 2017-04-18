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

  // Starts the transceiver.
  void Start(const InboundMessageCallback& message_received_callback);

  // Stops the transceiver.
  void Stop();

  // Sends a messaage to the specified address on the specified interface.
  void SendMessage(std::unique_ptr<DnsMessage> message,
                   const SocketAddress& dest_address,
                   uint32_t interface_index);

 private:
  struct InterfaceId {
    InterfaceId(const std::string& name, sa_family_t family)
        : name_(name), family_(family) {}

    std::string name_;
    sa_family_t family_;
  };

  bool InterfaceEnabled(netc_if_info_t* if_info);

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::vector<InterfaceId> enabled_interfaces_;
  std::vector<std::unique_ptr<MdnsInterfaceTransceiver>> interfaces_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MdnsTransceiver);
};

}  // namespace mdns
}  // namespace netconnector
