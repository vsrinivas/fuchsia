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

  void AddDisplayListener(fidl::InterfaceHandle<fuchsia::ui::display::DisplayListener>
                              display_listener_interface_handle) override;

  // For testing purposes only.
  const std::string& last_error() { return last_error_; }

  DisplayManager2(const DisplayManager2&) = delete;
  DisplayManager2(DisplayManager2&&) = delete;
  DisplayManager2& operator=(const DisplayManager2&) = delete;
  DisplayManager2& operator=(DisplayManager2&&) = delete;

 private:
  struct DisplayInfoHolder {
    // |id| assigned by the DisplayController.
    uint64_t id = 0;

    // Interface for the DisplayController that this display is connected to.
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller;

    fuchsia::ui::display::Info info;
  };

  class DisplayControllerHolder {
   public:
    DisplayControllerHolder(
        std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller,
        std::unique_ptr<DisplayControllerListener> listener,
        std::vector<DisplayInfoHolder> displays, bool has_ownership);
    ~DisplayControllerHolder();

    const std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr>& controller() const {
      return controller_;
    }
    DisplayControllerListener* listener() const { return listener_.get(); }
    const std::vector<DisplayInfoHolder>& displays() const { return displays_; }

    void set_has_ownership(bool has_ownership) { has_ownership_ = has_ownership; }
    bool has_ownership() const { return has_ownership_; }

    void AddDisplay(DisplayInfoHolder display);
    bool HasDisplayWithId(uint64_t display_id);
    std::optional<DisplayInfoHolder> RemoveDisplayWithId(uint64_t display_id);

   private:
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller_;
    std::unique_ptr<DisplayControllerListener> listener_;
    std::vector<DisplayInfoHolder> displays_;

    // Stores the latest value of the ClientOwnershipChange event from the
    // display controller.
    bool has_ownership_ = false;
  };

  void RemoveOnInvalid(DisplayControllerHolder* dc);
  void OnDisplaysChanged(DisplayControllerHolder* dc,
                         std::vector<fuchsia::hardware::display::Info> displays_added,
                         std::vector<uint64_t> displays_removed);
  void OnDisplayOwnershipChanged(DisplayControllerHolder* dc, bool has_ownership);

static DisplayInfoHolder NewDisplayInfoHolder(
    fuchsia::hardware::display::Info hardware_display_info,
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller);
  static void InvokeDisplayAddedForListener(
      const fidl::InterfacePtr<fuchsia::ui::display::DisplayListener>& listener,
      const DisplayInfoHolder& display_info_holder);
  static void InvokeDisplayOwnershipChangedForListener(
      const fidl::InterfacePtr<fuchsia::ui::display::DisplayListener>& listener,
      DisplayControllerHolder* dc, bool has_ownership);

  std::vector<std::unique_ptr<DisplayControllerHolder>> display_controllers_;
  fidl::InterfacePtrSet<fuchsia::ui::display::DisplayListener> display_listeners_;
  std::string last_error_;
};

}  // namespace display
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_MANAGER2_H_
