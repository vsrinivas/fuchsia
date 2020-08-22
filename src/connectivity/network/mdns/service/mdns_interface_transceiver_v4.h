// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_INTERFACE_TRANSCEIVER_V4_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_INTERFACE_TRANSCEIVER_V4_H_

#include "src/connectivity/network/mdns/service/mdns_interface_transceiver.h"

namespace mdns {

// Provides V4-specific behavior for abstract MdnsInterfaceTransceiver.
class MdnsInterfaceTransceiverV4 : public MdnsInterfaceTransceiver {
 public:
  MdnsInterfaceTransceiverV4(inet::IpAddress address, const std::string& name, uint32_t index,
                             Media media);

  virtual ~MdnsInterfaceTransceiverV4() override;

 protected:
  // MdnsInterfaceTransceiver overrides.
  int SetOptionDisableMulticastLoop() override;
  int SetOptionJoinMulticastGroup() override;
  int SetOptionOutboundInterface() override;
  int SetOptionUnicastTtl() override;
  int SetOptionMulticastTtl() override;
  int SetOptionFamilySpecific() override;
  int Bind() override;
  int SendTo(const void* buffer, size_t size, const inet::SocketAddress& address) override;

 public:
  // Disallow copy, assign and move.
  MdnsInterfaceTransceiverV4(const MdnsInterfaceTransceiverV4&) = delete;
  MdnsInterfaceTransceiverV4(MdnsInterfaceTransceiverV4&&) = delete;
  MdnsInterfaceTransceiverV4& operator=(const MdnsInterfaceTransceiverV4&) = delete;
  MdnsInterfaceTransceiverV4& operator=(MdnsInterfaceTransceiverV4&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_MDNS_INTERFACE_TRANSCEIVER_V4_H_
