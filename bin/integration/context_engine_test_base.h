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
    auto agent_host =
        std::make_unique<maxwell::ApplicationEnvironmentHostImpl>();
    agent_host->AddService<maxwell::ContextPubSub>(
        [this, url](fidl::InterfaceRequest<maxwell::ContextPubSub> request) {
          context_engine_->RegisterContextAgent(url, std::move(request));
        });
    StartAgent(url, std::move(agent_host));
  }

  maxwell::ContextEngine* context_engine() { return context_engine_.get(); }

 private:
  const maxwell::ContextEnginePtr context_engine_;
};
