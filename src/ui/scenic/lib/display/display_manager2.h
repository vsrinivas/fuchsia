// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_MANAGER2_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_MANAGER2_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/ui/display/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/zx/channel.h>

#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/display/display_controller_listener.h"

namespace scenic_impl {
namespace display {

class DisplayManager2 : public fuchsia::ui::display::DisplayManager {
 public:
  DisplayManager2();

  void AddDisplayController(
      std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller,
      std::unique_ptr<DisplayControllerListener> controller_listener);

  // |fuchsia::ui::display::DisplayManager|
  void AddDisplayListener(fidl::InterfaceHandle<fuchsia::ui::display::DisplayListener>
                              display_listener_interface_handle) override;

  // For testing purposes only.
  const std::string& last_error() { return last_error_; }

  DisplayManager2(const DisplayManager2&) = delete;
  DisplayManager2(DisplayManager2&&) = delete;
  DisplayManager2& operator=(const DisplayManager2&) = delete;
  DisplayManager2& operator=(DisplayManager2&&) = delete;

 private:
  struct DisplayInfoPrivate {
    // |id| assigned by the DisplayController.
    uint64_t id = 0;

    // Interface for the DisplayController that this display is connected to.
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller;

    // Also stores the key version of the DisplayRef.
    fuchsia::ui::display::Info info;
  };

  struct DisplayControllerPrivate {
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller;
    std::unique_ptr<DisplayControllerListener> listener;
    std::vector<DisplayInfoPrivate> displays;

    // The latest value of the ClientOwnershipChange event from the
    // display controller.
    bool has_ownership = false;
  };
  using DisplayControllerPrivateUniquePtr =
      std::unique_ptr<DisplayControllerPrivate, std::function<void(DisplayControllerPrivate*)>>;

  void RemoveOnInvalid(DisplayControllerPrivate* dc);
  void OnDisplaysChanged(DisplayControllerPrivate* dc,
                         std::vector<fuchsia::hardware::display::Info> displays_added,
                         std::vector<uint64_t> displays_removed);
  void OnDisplayOwnershipChanged(DisplayControllerPrivate* dc, bool has_ownership);

  static DisplayInfoPrivate NewDisplayInfoPrivate(
      fuchsia::hardware::display::Info hardware_display_info,
      std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller);
  static void InvokeDisplayAddedForListener(
      const fidl::InterfacePtr<fuchsia::ui::display::DisplayListener>& listener,
      const DisplayInfoPrivate& display_info_private);
  static void InvokeDisplayOwnershipChangedForListener(
      const fidl::InterfacePtr<fuchsia::ui::display::DisplayListener>& listener,
      DisplayControllerPrivate* dc, bool has_ownership);

  // Helper functions for lists of DisplayInfoPrivate.
  static bool HasDisplayWithId(const std::vector<DisplayManager2::DisplayInfoPrivate>& displays,
                               uint64_t display_id);

  static std::optional<DisplayManager2::DisplayInfoPrivate> RemoveDisplayWithId(
      std::vector<DisplayInfoPrivate>* displays, uint64_t display_id);

  std::vector<DisplayControllerPrivateUniquePtr> display_controllers_private_;
  fidl::InterfacePtrSet<fuchsia::ui::display::DisplayListener> display_listeners_;
  std::string last_error_;
};

}  // namespace display
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_MANAGER2_H_
