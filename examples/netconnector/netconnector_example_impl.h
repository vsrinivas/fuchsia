// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/channel.h>

#include "apps/netconnector/examples/netconnector_example/netconnector_example_params.h"
#include "apps/netconnector/lib/message_relay.h"
#include "application/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace examples {

class NetConnectorExampleImpl {
 public:
  NetConnectorExampleImpl(NetConnectorExampleParams* params);

  ~NetConnectorExampleImpl();

 private:
  void SendMessage(const std::string& message_string);

  void HandleReceivedMessage(std::vector<uint8_t> message);

  std::unique_ptr<modular::ApplicationContext> application_context_;
  netconnector::MessageRelay message_relay_;
  std::vector<std::string>::const_iterator conversation_iter_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetConnectorExampleImpl);
};

}  // namespace examples
