// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTS_LIB_CHILD_VIEW_WATCHER_CLIENT_H_
#define SRC_UI_TESTS_LIB_CHILD_VIEW_WATCHER_CLIENT_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <functional>

// A minimal server for fuchsia.ui.composition.ChildViewWatcher.  All it does is
// forward the values it receives to the functions set by the user.
class ChildViewWatcherClient {
 public:
  struct Callbacks {
    // Called when GetStatus returns.
    std::function<void(fuchsia::ui::composition::ChildViewStatus)> on_get_status{};
    // Called when GetViewRef returns.
    std::function<void(fuchsia::ui::views::ViewRef)> on_get_view_ref{};
  };

  ChildViewWatcherClient(
      fidl::InterfaceHandle<fuchsia::ui::composition::ChildViewWatcher> client_end,
      Callbacks callbacks);

 private:
  // Schedule* methods ensure that changes to the status are continuously
  // communicated to the test fixture. This is because the statuses may
  // change several times before they settle into the value we need.

  void ScheduleGetStatus();

  void ScheduleGetViewRef();

  Callbacks callbacks_;
  fidl::InterfacePtr<fuchsia::ui::composition::ChildViewWatcher> client_end_;
};

#endif  // SRC_UI_TESTS_LIB_CHILD_VIEW_WATCHER_CLIENT_H_
