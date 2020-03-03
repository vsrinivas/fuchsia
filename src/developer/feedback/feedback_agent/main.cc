// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/logger.h>

#include <memory>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/feedback_agent/feedback_agent.h"
#include "src/developer/feedback/feedback_agent/feedback_id.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"feedback"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  if (!feedback::InitializeFeedbackId(feedback::kFeedbackIdPath)) {
    FX_LOGS(ERROR) << "Error initializing feedback id";
  }

  auto inspector = std::make_unique<sys::ComponentInspector>(context.get());
  inspect::Node& root_node = inspector->root();

  std::unique_ptr<feedback::FeedbackAgent> agent =
      feedback::FeedbackAgent::TryCreate(loop.dispatcher(), context->svc(), &root_node);
  if (!agent) {
    return EXIT_FAILURE;
  }

  //  TODO(fxb/47000): re-enable once OOM issues are resolved.
  //  agent->SpawnSystemLogRecorder();

  context->outgoing()->AddPublicService(
      fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider>(
          [&agent](fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
            agent->HandleDataProviderRequest(std::move(request));
          }));

  loop.Run();

  return EXIT_SUCCESS;
}
