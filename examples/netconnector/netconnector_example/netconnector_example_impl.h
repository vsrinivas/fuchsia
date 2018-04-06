// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/channel.h>

#include "garnet/examples/netconnector/netconnector_example/netconnector_example_params.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/netconnector/cpp/message_relay.h"

namespace examples {

class NetConnectorExampleImpl {
 public:
  NetConnectorExampleImpl(NetConnectorExampleParams* params,
                          fxl::Closure quit_callback);

  ~NetConnectorExampleImpl();

 private:
  void SendMessage(const std::string& message_string);

  void HandleReceivedMessage(std::vector<uint8_t> message);

  fxl::Closure quit_callback_;
  std::unique_ptr<component::ApplicationContext> application_context_;
  netconnector::MessageRelay message_relay_;
  std::vector<std::string>::const_iterator conversation_iter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NetConnectorExampleImpl);
};

}  // namespace examples
