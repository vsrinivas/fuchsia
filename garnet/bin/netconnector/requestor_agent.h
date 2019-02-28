// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETCONNECTOR_REQUESTOR_AGENT_H_
#define GARNET_BIN_NETCONNECTOR_REQUESTOR_AGENT_H_

#include <arpa/inet.h>

#include <memory>

#include "garnet/bin/netconnector/message_transceiver.h"
#include "garnet/lib/inet/socket_address.h"
#include "src/lib/files/unique_fd.h"

namespace netconnector {

class NetConnectorImpl;

class RequestorAgent : public MessageTransceiver {
 public:
  static std::unique_ptr<RequestorAgent> Create(
      const inet::SocketAddress& address, const std::string& service_name,
      zx::channel local_channel, NetConnectorImpl* owner);

  ~RequestorAgent() override;

 protected:
  // MessageTransceiver overrides.
  void OnVersionReceived(uint32_t version) override;

  void OnServiceNameReceived(const std::string& service_name) override;

  void OnConnectionClosed() override;

 private:
  RequestorAgent(fxl::UniqueFD socket_fd, const std::string& service_name,
                 zx::channel local_channel, NetConnectorImpl* owner);

  std::string service_name_;
  zx::channel local_channel_;
  NetConnectorImpl* owner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RequestorAgent);
};

}  // namespace netconnector

#endif  // GARNET_BIN_NETCONNECTOR_REQUESTOR_AGENT_H_
