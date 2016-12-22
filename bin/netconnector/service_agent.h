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

class ServiceAgent : public MessageTransciever {
 public:
  static std::unique_ptr<ServiceAgent> Create(ftl::UniqueFD socket_fd,
                                              NetConnectorImpl* owner);

  ~ServiceAgent();

 protected:
  // MessageTransciever overrides.
  void OnVersionReceived(uint32_t version) override;

  void OnServiceNameReceived(const std::string& service_name) override;

  void OnConnectionClosed() override;

 private:
  ServiceAgent(ftl::UniqueFD socket_fd, NetConnectorImpl* owner);

  NetConnectorImpl* owner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ServiceAgent);
};

}  // namespace netconnector
