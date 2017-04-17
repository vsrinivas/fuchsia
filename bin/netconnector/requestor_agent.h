// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <arpa/inet.h>

#include <memory>

#include "apps/netconnector/src/message_transceiver.h"
#include "apps/netconnector/src/socket_address.h"
#include "lib/ftl/files/unique_fd.h"

namespace netconnector {

class NetConnectorImpl;

class RequestorAgent : public MessageTransciever {
 public:
  static std::unique_ptr<RequestorAgent> Create(const SocketAddress& address,
                                                const std::string& service_name,
                                                mx::channel local_channel,
                                                NetConnectorImpl* owner);

  ~RequestorAgent() override;

 protected:
  // MessageTransciever overrides.
  void OnVersionReceived(uint32_t version) override;

  void OnServiceNameReceived(const std::string& service_name) override;

  void OnConnectionClosed() override;

 private:
  RequestorAgent(ftl::UniqueFD socket_fd,
                 const std::string& service_name,
                 mx::channel local_channel,
                 NetConnectorImpl* owner);

  std::string service_name_;
  mx::channel local_channel_;
  NetConnectorImpl* owner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RequestorAgent);
};

}  // namespace netconnector
