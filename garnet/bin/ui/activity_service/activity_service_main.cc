// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include <src/lib/fxl/logging.h>

#include "garnet/bin/ui/activity_service/activity_service_app.h"
#include "garnet/bin/ui/activity_service/state_machine_driver.h"

int main(void) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  async_set_default_dispatcher(loop.dispatcher());

  std::unique_ptr<sys::ComponentContext> startup_context = sys::ComponentContext::Create();

  auto driver = std::make_unique<activity_service::StateMachineDriver>(loop.dispatcher());
  activity_service::ActivityServiceApp app(std::move(driver), loop.dispatcher());

  startup_context->outgoing()->AddPublicService<fuchsia::ui::activity::Tracker>(
      [&app](fidl::InterfaceRequest<fuchsia::ui::activity::Tracker> request) {
        app.AddTrackerBinding(std::move(request));
      });

  FXL_LOG(INFO) << "activity-service: Starting service";
  loop.Run();
  async_set_default_dispatcher(nullptr);
  return 0;
}
