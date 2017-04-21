// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/netconnector/src/mdns/mdns_interface_transceiver.h"

namespace netconnector {
namespace mdns {

// Provides V6-specific behavior for abstract MdnsInterfaceTransceiver.
class MdnsInterfaceTransceiverV6 : public MdnsInterfaceTransceiver {
 public:
  virtual ~MdnsInterfaceTransceiverV6() override;

 protected:
  // MdnsInterfaceTransceiver overrides.
  int SetOptionJoinMulticastGroup() override;
  int SetOptionOutboundInterface() override;
  int SetOptionUnicastTtl() override;
  int SetOptionMulticastTtl() override;
  int SetOptionFamilySpecific() override;
  int Bind() override;
  int SendTo(const void* buffer,
             size_t size,
             const SocketAddress& address) override;

 private:
  MdnsInterfaceTransceiverV6(const netc_if_info_t& if_info, uint32_t index);

  friend class MdnsInterfaceTransceiver;  // For constructor.
};

}  // namespace mdns
}  // namespace netconnector
