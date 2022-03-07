// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTS_LIB_PARENT_VIEWPORT_WATCHER_CLIENT_H_
#define SRC_UI_TESTS_LIB_PARENT_VIEWPORT_WATCHER_CLIENT_H_

#include <fuchsia/ui/composition/cpp/fidl.h>

// A minimal server for fuchsia.ui.composition.ParentViewportWatcher.  All it
// does is forward the values it receives to the functions set by the user.
class ParentViewportWatcherClient {
 public:
  // The functions to call on protocol events.
  struct Callbacks {
    // Called when GetLayout returns.
    std::function<void(fuchsia::ui::composition::LayoutInfo info)> on_get_layout{};
    // Called when GetStatus returns.
    std::function<void(fuchsia::ui::composition::ParentViewportStatus info)> on_status_info{};
  };

  ParentViewportWatcherClient(
      fidl::InterfaceHandle<fuchsia::ui::composition::ParentViewportWatcher> client_end,
      ParentViewportWatcherClient::Callbacks callbacks);

 private:
  // Schedule* methods ensure that changes to the status are continuously
  // communicated to the test fixture. This is because the statuses may
  // change several times before they settle into the value we need.

  void ScheduleGetLayout();

  void ScheduleStatusInfo();

  Callbacks callbacks_;
  fidl::InterfacePtr<fuchsia::ui::composition::ParentViewportWatcher> client_end_;
};

#endif  // SRC_UI_TESTS_LIB_PARENT_VIEWPORT_WATCHER_CLIENT_H_
