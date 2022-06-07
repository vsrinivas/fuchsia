// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_RUNTIME_CONNECTOR_IMPL_H_
#define LIB_DRIVER2_RUNTIME_CONNECTOR_IMPL_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/fdf/cpp/channel.h>

#include <string>
#include <unordered_map>

namespace driver {

// This class is not thread-safe and should be used with a synchronized dispatcher.
class RuntimeConnectorImpl : public fidl::WireServer<fuchsia_driver_framework::RuntimeConnector> {
 public:
  using RegisterProtocolHandler = fit::function<zx_status_t(fdf::Channel)>;

  explicit RuntimeConnectorImpl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // Registers a supported protocol with the given |protocol_name|.
  // |handler| will be called when a client tries to connect to |protocol_name|.
  // If a handler has already been registered for the protocol, it will be replaced by the
  // new handler.
  void RegisterProtocol(std::string protocol_name, RegisterProtocolHandler handler);

  // fuchsia.driver.framework.RuntimeConnector implementation
  void ListProtocols(ListProtocolsRequestView request,
                     ListProtocolsCompleter::Sync& completer) override;
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override;

 private:
  async_dispatcher_t* dispatcher_;

  // Maps from registered protocol to the handler that will be called by |Connect|.
  std::unordered_map<std::string, RegisterProtocolHandler> protocol_to_handler_;
};

}  // namespace driver

#endif  // LIB_DRIVER2_RUNTIME_CONNECTOR_IMPL_H_
