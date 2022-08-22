// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/app.h"

#include <fuchsia/ui/keyboard/focus/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/event.h>
#include <zircon/status.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "src/lib/files/file.h"

namespace root_presenter {

App::App(sys::ComponentContext* component_context, fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)),
      inspector_(component_context),
      virtual_keyboard_coordinator_(component_context),
      focus_dispatcher_(component_context->svc(), virtual_keyboard_coordinator_.GetWeakPtr()) {
  FX_DCHECK(component_context);

  component_context->svc()->Connect(scenic_.NewRequest());
  scenic_.set_error_handler([](zx_status_t error) {
    FX_LOGS(WARNING) << "Scenic died with error " << zx_status_get_string(error);
  });

  int32_t display_startup_rotation_adjustment = 0;
  {
    std::string rotation_value;
    if (files::ReadFileToString("/config/data/display_rotation", &rotation_value)) {
      display_startup_rotation_adjustment = atoi(rotation_value.c_str());
      FX_LOGS(INFO) << "Display rotation adjustment applied: "
                    << display_startup_rotation_adjustment << " degrees.";
    }
  }

  // Set up root session.
  // Create session and session listener.
  fuchsia::ui::scenic::SessionPtr session_ptr;
  fuchsia::ui::views::FocuserPtr focuser_ptr;
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> session_listener;
  auto session_request = session_ptr.NewRequest();

  // Wrap root session for convenience and create valid session listener.
  auto session =
      std::make_unique<scenic::Session>(std::move(session_ptr), session_listener.NewRequest());

  // Create scenic session.
  scenic_->CreateSession2(std::move(session_request), session_listener.Bind(),
                          focuser_ptr.NewRequest());

  presentation_ = std::make_unique<Presentation>(
      inspector_.root().CreateChild(inspector_.root().UniqueName("presentation-")),
      component_context, scenic_.get(), std::move(session), std::move(focuser_ptr),
      display_startup_rotation_adjustment);

  FX_DCHECK(scenic_);
  FX_DCHECK(presentation_)
      << "All service handlers must be set up and published prior to loop.Run() in main.cc";
}

}  // namespace root_presenter
