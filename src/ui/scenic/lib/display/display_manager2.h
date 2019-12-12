// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_MANAGER2_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_MANAGER2_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/ui/display/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/zx/channel.h>
#include <zircon/pixelformat.h>

#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/display/display_controller.h"
#include "src/ui/scenic/lib/display/display_controller_listener.h"

namespace scenic_impl {
namespace display {

// Implements the |fuchsia::ui::display::DisplayManager| protocol. Notifies protocol clients
// of new or removed displays and allows changing of display configuration. Every display is
// associated with a DisplayRef which can also be used as a parameter to other apis (e.g. Scenic).
// Additionally, allows an internal (within Scenic) client to claim the display and 
class DisplayManager2 : public fuchsia::ui::display::DisplayManager {
 public:
  DisplayManager2();

  // |fuchsia::ui::display::DisplayManager|
  void AddDisplayListener(fidl::InterfaceHandle<fuchsia::ui::display::DisplayListener>
                              display_listener_interface_handle) override;

  // Remaining methods are not part of the FIDL protocol.

  // Called by initializing code whenever a new DisplayzController is discovered, or by tests.
  void AddDisplayController(
      std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller,
      std::unique_ptr<DisplayControllerListener> controller_listener);

  DisplayControllerUniquePtr ClaimDisplay(zx_koid_t display_ref_koid);
  DisplayControllerUniquePtr ClaimFirstDisplayDeprecated();

  const fxl::WeakPtr<DisplayManager2> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

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

    zx_koid_t display_ref_koid = 0;

    std::vector<zx_pixel_format_t> pixel_formats;

    // Interface for the DisplayController that this display is connected to.
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller;

    // Also stores the key version of the DisplayRef.
    fuchsia::ui::display::Info info;
  };

  // Internal data structure that holds the DisplayController interface and
  // associated info (listener, list of Displays).
  struct DisplayControllerPrivate {
    // If a a client has called ClaimDisplay(), this will be non-null and point
    // to the DisplayController passed to the client. This pointer is nulled
    // out by the custom deleter for the DisplayController.
    DisplayController* claimed_dc = nullptr;

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

  std::tuple<DisplayControllerPrivate*, DisplayInfoPrivate*> FindDisplay(
      zx_koid_t display_ref_koid);
  DisplayControllerPrivate* FindDisplayControllerPrivate(DisplayController* dc);

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

  fxl::WeakPtrFactory<DisplayManager2> weak_factory_;  // must be last
};

}  // namespace display
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_MANAGER2_H_
