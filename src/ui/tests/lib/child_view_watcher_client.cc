// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tests/lib/child_view_watcher_client.h"

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

using fuchsia::ui::composition::ChildViewStatus;
using fuchsia::ui::composition::ChildViewWatcher;

ChildViewWatcherClient::ChildViewWatcherClient(fidl::InterfaceHandle<ChildViewWatcher> client_end,
                                               ChildViewWatcherClient::Callbacks callbacks)
    // Subtle: callbacks need to be initialized before a call to Bind, else
    // we may receive messages before we install the message handlers.
    : callbacks_(std::move(callbacks)), client_end_(client_end.Bind()) {
  client_end_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "watcher error: " << zx_status_get_string(status);
  });
  ScheduleGetStatus();
  ScheduleGetViewRef();
}

void ChildViewWatcherClient::ScheduleGetStatus() {
  client_end_->GetStatus([this](ChildViewStatus status) {
    this->callbacks_.on_get_status(status);
    this->ScheduleGetStatus();
  });
}

void ChildViewWatcherClient::ScheduleGetViewRef() {
  client_end_->GetViewRef([this](fuchsia::ui::views::ViewRef view_ref) {
    this->callbacks_.on_get_view_ref(std::move(view_ref));
    this->ScheduleGetViewRef();
  });
}
