// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/channel.h>

#include "lib/app/cpp/application_context.h"
#include "garnet/examples/netconnector/netconnector_example_params.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"
#include "lib/netconnector/cpp/message_relay.h"

namespace examples {

class NetConnectorExampleImpl {
 public:
  NetConnectorExampleImpl(NetConnectorExampleParams* params);

  ~NetConnectorExampleImpl();

 private:
  void SendMessage(const std::string& message_string);

  void HandleReceivedMessage(std::vector<uint8_t> message);

  std::unique_ptr<app::ApplicationContext> application_context_;
  netconnector::MessageRelay message_relay_;
  std::vector<std::string>::const_iterator conversation_iter_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetConnectorExampleImpl);
};

}  // namespace examples
