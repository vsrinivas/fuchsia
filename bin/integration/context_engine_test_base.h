// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/src/integration/test.h"

// Base fixture to support test cases requiring Context Engine.
class ContextEngineTestBase : public MaxwellTestBase {
 public:
  ContextEngineTestBase()
      : context_engine_(ConnectToService<maxwell::ContextEngine>(
            "file:///system/apps/context_engine")) {}

 protected:
  void StartContextAgent(const std::string& url) {
    auto agent_host = std::make_unique<maxwell::ApplicationEnvironmentHostImpl>(
        root_environment);
    agent_host->AddService<maxwell::ContextPublisher>(
        [this, url](fidl::InterfaceRequest<maxwell::ContextPublisher> request) {
          context_engine_->RegisterPublisher(url, std::move(request));
        });
    agent_host->AddService<maxwell::ContextSubscriber>(
        [this, url](fidl::InterfaceRequest<maxwell::ContextSubscriber> request) {
          context_engine_->RegisterSubscriber(url, std::move(request));
        });
    StartAgent(url, std::move(agent_host));
  }

  maxwell::ContextEngine* context_engine() { return context_engine_.get(); }

 private:
  const maxwell::ContextEnginePtr context_engine_;
};
