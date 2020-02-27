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
  feedback::FeedbackAgent agent(&root_node);

  //  TODO(fxb/47000): re-enable once OOM issues are resolved.
  //  agent.SpawnSystemLogRecorder();

  // We spawn a new process capable of handling fuchsia.feedback.DataProvider requests on every
  // incoming request. This has the advantage of tying each request to a different process that can
  // be cleaned up once it is done or after a timeout and take care of dangling threads for
  // instance, cf. CF-756.
  context->outgoing()->AddPublicService(
      fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider>(
          [&](fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
            agent.SpawnNewDataProvider(std::move(request));
          }));

  loop.Run();

  return EXIT_SUCCESS;
}
