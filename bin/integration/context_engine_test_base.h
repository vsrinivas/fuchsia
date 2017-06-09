// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/services/user/scope.fidl.h"
#include "apps/maxwell/src/integration/test.h"

namespace maxwell {

// Base fixture to support test cases requiring Context Engine.
class ContextEngineTestBase : public MaxwellTestBase {
 public:
  ContextEngineTestBase()
      : context_engine_(ConnectToService<ContextEngine>(
            "file:///system/apps/context_engine")) {}

 protected:
  void StartContextAgent(const std::string& url) {
    auto agent_host =
        std::make_unique<ApplicationEnvironmentHostImpl>(root_environment);
    agent_host->AddService<ContextPublisher>(
        [this, url](fidl::InterfaceRequest<ContextPublisher> request) {
          auto scope = ComponentScope::New();
          auto agent_scope = AgentScope::New();
          agent_scope->url = url;
          scope->set_agent_scope(std::move(agent_scope));
          context_engine_->GetPublisher(std::move(scope), std::move(request));
        });
    agent_host->AddService<ContextProvider>(
        [this, url](fidl::InterfaceRequest<ContextProvider> request) {
          auto scope = ComponentScope::New();
          auto agent_scope = AgentScope::New();
          agent_scope->url = url;
          scope->set_agent_scope(std::move(agent_scope));
          context_engine_->GetProvider(std::move(scope), std::move(request));
        });
    StartAgent(url, std::move(agent_host));
  }

  ContextEngine* context_engine() { return context_engine_.get(); }

 private:
  const ContextEnginePtr context_engine_;
};

}  // namespace maxwell
