// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <arpa/inet.h>

#include <memory>

#include "apps/netconnector/src/message_transceiver.h"
#include "lib/ftl/files/unique_fd.h"

namespace netconnector {

class NetConnectorImpl;

class RequestorAgent : public MessageTransciever {
 public:
  static std::unique_ptr<RequestorAgent> Create(
      const std::string& address,
      uint16_t port,
      const std::string& responder_name,
      mx::channel local_channel,
      NetConnectorImpl* owner);

  ~RequestorAgent() override;

 protected:
  // MessageTransciever overrides.
  void OnVersionReceived(uint32_t version) override;

  void OnResponderNameReceived(std::string responder_name) override;

  void OnConnectionClosed() override;

 private:
  static void SetPort(struct sockaddr* addr, uint16_t port);

  RequestorAgent(ftl::UniqueFD socket_fd,
                 const std::string& responder_name,
                 mx::channel local_channel,
                 NetConnectorImpl* owner);

  std::string responder_name_;
  mx::channel local_channel_;
  NetConnectorImpl* owner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RequestorAgent);
};

}  // namespace netconnector
