// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "apps/netconnector/services/netconnector.fidl.h"
#include "apps/netconnector/src/listener.h"
#include "apps/netconnector/src/requestor_agent.h"
#include "apps/netconnector/src/responder_agent.h"
#include "apps/netconnector/src/netconnector_params.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace netconnector {

class NetConnectorImpl : public NetConnector {
 public:
  NetConnectorImpl(NetConnectorParams* params);

  ~NetConnectorImpl();

  ResponderPtr GetResponder(const std::string responder_name);

  void ReleaseRequestorAgent(RequestorAgent* requestor_agent);

  void ReleaseResponderAgent(ResponderAgent* responder_agent);

  // NetConnector implementation.
  void SetHostName(const fidl::String& host_name) override;

  void RegisterResponder(const fidl::String& name,
                         const fidl::String& url) override;

  void RegisterDevice(const fidl::String& name,
                      const fidl::String& address) override;

  void RequestConnection(const fidl::String& device_name,
                         const fidl::String& responder_name,
                         mx::channel channel) override;

 private:
  static constexpr uint16_t kPort = 7777;

  void AddRequestorAgent(std::unique_ptr<RequestorAgent> requestor_agent);

  void AddResponderAgent(std::unique_ptr<ResponderAgent> responder_agent);

  NetConnectorParams* params_;
  std::unique_ptr<modular::ApplicationContext> application_context_;
  fidl::BindingSet<NetConnector> bindings_;
  Listener listener_;
  std::unordered_map<RequestorAgent*, std::unique_ptr<RequestorAgent>>
      requestor_agents_;
  std::unordered_map<ResponderAgent*, std::unique_ptr<ResponderAgent>>
      responder_agents_;

  FTL_DISALLOW_COPY_AND_ASSIGN(NetConnectorImpl);
};

}  // namespace netconnector
