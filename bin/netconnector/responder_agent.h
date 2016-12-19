// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/netconnector/services/netconnector.fidl.h"
#include "apps/netconnector/src/message_transceiver.h"
#include "lib/ftl/files/unique_fd.h"

namespace netconnector {

class NetConnectorImpl;

class ResponderAgent : public MessageTransciever {
 public:
  static std::unique_ptr<ResponderAgent> Create(ftl::UniqueFD socket_fd,
                                                NetConnectorImpl* owner);

  ~ResponderAgent();

 protected:
  // MessageTransciever overrides.
  void OnVersionReceived(uint32_t version) override;

  void OnResponderNameReceived(std::string responder_name) override;

  void OnConnectionClosed() override;

 private:
  ResponderAgent(ftl::UniqueFD socket_fd, NetConnectorImpl* owner);

  NetConnectorImpl* owner_;
  ResponderPtr responder_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ResponderAgent);
};

}  // namespace netconnector
