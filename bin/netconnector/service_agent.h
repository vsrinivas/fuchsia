// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/netconnector/message_transceiver.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/netconnector/fidl/netconnector.fidl.h"

namespace netconnector {

class NetConnectorImpl;

class ServiceAgent : public MessageTransceiver {
 public:
  static std::unique_ptr<ServiceAgent> Create(fxl::UniqueFD socket_fd,
                                              NetConnectorImpl* owner);

  ~ServiceAgent();

 protected:
  // MessageTransceiver overrides.
  void OnVersionReceived(uint32_t version) override;

  void OnServiceNameReceived(const std::string& service_name) override;

  void OnConnectionClosed() override;

 private:
  ServiceAgent(fxl::UniqueFD socket_fd, NetConnectorImpl* owner);

  NetConnectorImpl* owner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceAgent);
};

}  // namespace netconnector
