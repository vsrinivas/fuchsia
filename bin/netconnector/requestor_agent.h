// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <arpa/inet.h>

#include <memory>

#include "garnet/bin/netconnector/message_transceiver.h"
#include "garnet/bin/netconnector/socket_address.h"
#include "lib/fxl/files/unique_fd.h"

namespace netconnector {

class NetConnectorImpl;

class RequestorAgent : public MessageTransceiver {
 public:
  static std::unique_ptr<RequestorAgent> Create(const SocketAddress& address,
                                                const std::string& service_name,
                                                mx::channel local_channel,
                                                NetConnectorImpl* owner);

  ~RequestorAgent() override;

 protected:
  // MessageTransceiver overrides.
  void OnVersionReceived(uint32_t version) override;

  void OnServiceNameReceived(const std::string& service_name) override;

  void OnConnectionClosed() override;

 private:
  RequestorAgent(fxl::UniqueFD socket_fd,
                 const std::string& service_name,
                 mx::channel local_channel,
                 NetConnectorImpl* owner);

  std::string service_name_;
  mx::channel local_channel_;
  NetConnectorImpl* owner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RequestorAgent);
};

}  // namespace netconnector
