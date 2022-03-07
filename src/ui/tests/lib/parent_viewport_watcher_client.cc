// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tests/lib/parent_viewport_watcher_client.h"

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::ParentViewportStatus;

ParentViewportWatcherClient::ParentViewportWatcherClient(
    fidl::InterfaceHandle<fuchsia::ui::composition::ParentViewportWatcher> client_end,
    ParentViewportWatcherClient::Callbacks callbacks)
    // Subtle: callbacks are initialized before a call to Bind, so that we don't
    // receive messages from the client end before the callbacks are installed.
    : callbacks_(std::move(callbacks)), client_end_(client_end.Bind()) {
  client_end_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "watcher error: " << zx_status_get_string(status);
  });
  // Kick off hanging get requests now.
  ScheduleGetLayout();
  ScheduleStatusInfo();
}

void ParentViewportWatcherClient::ScheduleGetLayout() {
  client_end_->GetLayout([this](LayoutInfo l) {
    this->callbacks_.on_get_layout(std::move(l));
    ScheduleGetLayout();
  });
}

void ParentViewportWatcherClient::ScheduleStatusInfo() {
  client_end_->GetStatus([this](ParentViewportStatus s) {
    this->callbacks_.on_status_info(s);
    ScheduleStatusInfo();
  });
}
