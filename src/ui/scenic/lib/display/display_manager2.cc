// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_manager2.h"

#include <lib/fidl/cpp/clone.h>

#include <iterator>
#include <string>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/display/display_controller.h"

namespace scenic_impl {
namespace display {

static fuchsia::ui::display::DisplayRef NewDisplayRef() {
  fuchsia::ui::display::DisplayRef display_ref;
  // Safe and valid event, by construction.
  zx_status_t status = zx::event::create(/*flags*/ 0u, &display_ref.reference);
  FXL_DCHECK(status == ZX_OK);
  // Reduce rights to only what's necessary.
  status = display_ref.reference.replace(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_INSPECT,
                                         &display_ref.reference);
  FXL_DCHECK(status == ZX_OK);
  return display_ref;
}

static fuchsia::ui::display::DisplayRef DuplicateDisplayRef(
    const fuchsia::ui::display::DisplayRef& original_ref) {
  fuchsia::ui::display::DisplayRef display_ref;
  // Safe and valid event, by construction. Don't allow further duplication.
  zx_status_t status = original_ref.reference.duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_INSPECT,
                                                        &display_ref.reference);
  FXL_DCHECK(status == ZX_OK);
  return display_ref;
}

DisplayManager2::DisplayManager2() : weak_factory_(this) {}

void DisplayManager2::AddDisplayController(
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller,
    std::unique_ptr<DisplayControllerListener> controller_listener) {
  auto display_controller_private = DisplayControllerPrivateUniquePtr(
      new DisplayControllerPrivate{.controller = std::move(controller),
                                   .listener = std::move(controller_listener),
                                   .displays = std::vector<DisplayInfoPrivate>(),
                                   .has_ownership = false},
      [](DisplayControllerPrivate* dc) {
        if (dc->listener) {
          dc->listener->ClearCallbacks();
        }
        delete dc;
      });

  DisplayControllerPrivate* dc = display_controller_private.get();

  display_controllers_private_.push_back(std::move(display_controller_private));

  auto on_invalid_cb = [this, dc]() { RemoveOnInvalid(dc); };

  auto displays_changed_cb = [this, dc](std::vector<fuchsia::hardware::display::Info> added,
                                        std::vector<uint64_t> removed) {
    OnDisplaysChanged(dc, std::move(added), std::move(removed));
  };

  auto display_ownership_changed_cb = [this, dc](bool has_ownership) {
    OnDisplayOwnershipChanged(dc, has_ownership);
  };

  // Note: These callbacks are all cleared in the destructor for |dc|.
  dc->listener->InitializeCallbacks(std::move(on_invalid_cb), std::move(displays_changed_cb),
                                    std::move(display_ownership_changed_cb));
}

void DisplayManager2::InvokeDisplayAddedForListener(
    const fidl::InterfacePtr<fuchsia::ui::display::DisplayListener>& listener,
    const DisplayInfoPrivate& display_info_private) {
  auto display_info_clone = fidl::Clone(display_info_private.info);
  display_info_clone.set_display_ref(DuplicateDisplayRef(display_info_clone.display_ref()));
  listener->OnDisplayAdded(std::move(display_info_clone));
}

bool DisplayManager2::HasDisplayWithId(
    const std::vector<DisplayManager2::DisplayInfoPrivate>& displays, uint64_t display_id) {
  auto display_it = std::find_if(displays.begin(), displays.end(),
                                 [display_id](const DisplayInfoPrivate& display_info) {
                                   return display_info.id == display_id;
                                 });
  return display_it != displays.end();
}

void DisplayManager2::InvokeDisplayOwnershipChangedForListener(
    const fidl::InterfacePtr<fuchsia::ui::display::DisplayListener>& listener,
    DisplayControllerPrivate* dc, bool has_ownership) {
  if (dc->displays.empty()) {
    return;
  }
  std::vector<fuchsia::ui::display::DisplayRef> display_refs;
  for (auto& display_info_private : dc->displays) {
    display_refs.push_back(DuplicateDisplayRef(display_info_private.info.display_ref()));
  }
  listener->OnDisplayOwnershipChanged(std::move(display_refs), has_ownership);
}

void DisplayManager2::AddDisplayListener(
    fidl::InterfaceHandle<fuchsia::ui::display::DisplayListener>
        display_listener_interface_handle) {
  fuchsia::ui::display::DisplayListenerPtr display_listener =
      display_listener_interface_handle.Bind();

  for (auto& display_controller : display_controllers_private_) {
    // Deliver OnDisplayAdded events for all the displays that currently exist.
    for (auto& display : display_controller->displays) {
      InvokeDisplayAddedForListener(display_listener, display);
    }

    // Notify the client if we have ownership of the display controller.
    if (display_controller->has_ownership) {
      InvokeDisplayOwnershipChangedForListener(display_listener, display_controller.get(),
                                               display_controller->has_ownership);
    }
  }

  display_listeners_.AddInterfacePtr(std::move(display_listener));
}

DisplayControllerUniquePtr DisplayManager2::ClaimFirstDisplayDeprecated() {
  for (auto& display_controller_private : display_controllers_private_) {
    if (display_controller_private->displays.size() > 0) {
      return ClaimDisplay(display_controller_private->displays[0].display_ref_koid);
    }
  }
  return nullptr;
}

DisplayControllerUniquePtr DisplayManager2::ClaimDisplay(zx_koid_t display_ref_koid) {
  DisplayControllerPrivate* dc_private = nullptr;
  DisplayInfoPrivate* display_info_private = nullptr;

  std::tie(dc_private, display_info_private) = FindDisplay(display_ref_koid);

  if (dc_private == nullptr) {
    return nullptr;
  }

  if (dc_private->claimed_dc) {
    return nullptr;
  }

  // Create a list of displays.
  std::vector<Display2> displays;
  for (auto& display_info_private : dc_private->displays) {
    displays.emplace_back(display_info_private.id, display_info_private.info.modes(),
                          display_info_private.pixel_formats);
  }

  auto custom_deleter = [weak = GetWeakPtr()](DisplayController* dc) {
    if (weak) {
      DisplayControllerPrivate* dc_private = weak->FindDisplayControllerPrivate(dc);
      if (dc_private) {
        dc_private->claimed_dc = nullptr;
        dc_private->listener->SetOnVsyncCallback(nullptr);
      }
    }
    delete dc;
  };

  DisplayControllerUniquePtr display_controller =
      DisplayControllerUniquePtr(new DisplayController(std::move(displays), dc_private->controller),
                                 std::move(custom_deleter));

  // This raw pointer is cleared in the custom deleter above.
  dc_private->claimed_dc = display_controller.get();

  // This callback is cleared in the custom deleter above.
  dc_private->listener->SetOnVsyncCallback(
      [dc_private](uint64_t display_id, uint64_t timestamp, std::vector<uint64_t> images) {
        if (!dc_private->claimed_dc) {
          FXL_LOG(WARNING)
              << "DisplayManager: Couldn't find display controller matching to vsync callback.";
          FXL_DCHECK(false);
          return;
        }
        // Since the number of displays will be very low (and often only == 1), performance is
        // usually better iterating instead of using a map.
        for (auto& display : *(dc_private->claimed_dc->displays())) {
          if (display.display_id() == display_id) {
            display.OnVsync(zx::time(timestamp), std::move(images));
            return;
          }
        }
        FXL_LOG(WARNING) << "DisplayManager: Couldn't find display matching to vsync callback.";
        FXL_DCHECK(false);
      });
  return display_controller;
}

std::optional<DisplayManager2::DisplayInfoPrivate> DisplayManager2::RemoveDisplayWithId(
    std::vector<DisplayInfoPrivate>* displays, uint64_t display_id) {
  auto display_it = std::find_if(
      displays->begin(), displays->end(),
      [display_id](DisplayInfoPrivate& display_info) { return display_info.id == display_id; });
  if (display_it != displays->end()) {
    auto removed_display = std::make_optional<DisplayInfoPrivate>(std::move(*display_it));
    displays->erase(display_it);
    return removed_display;
  } else {
    return std::optional<DisplayInfoPrivate>();
  }
}

DisplayManager2::DisplayControllerPrivate* DisplayManager2::FindDisplayControllerPrivate(
    DisplayController* dc) {
  for (auto& display_controller_private : display_controllers_private_) {
    if (display_controller_private->claimed_dc == dc) {
      return display_controller_private.get();
    }
  }
  return nullptr;
}

std::tuple<DisplayManager2::DisplayControllerPrivate*, DisplayManager2::DisplayInfoPrivate*>
DisplayManager2::FindDisplay(zx_koid_t display_ref_koid) {
  for (auto& display_controller_private : display_controllers_private_) {
    for (auto& display_private : display_controller_private->displays) {
      if (display_private.display_ref_koid == display_ref_koid) {
        return {display_controller_private.get(), &display_private};
      }
    }
  }
  return {nullptr, nullptr};
}

DisplayManager2::DisplayInfoPrivate DisplayManager2::NewDisplayInfoPrivate(
    fuchsia::hardware::display::Info hardware_display_info,
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller) {
  fuchsia::ui::display::Info display_info;
  display_info.set_display_ref(NewDisplayRef());
  display_info.set_modes(std::move(hardware_display_info.modes));
  display_info.set_manufacturer_name(hardware_display_info.manufacturer_name);
  display_info.set_monitor_name(hardware_display_info.monitor_name);

  return DisplayInfoPrivate{
      hardware_display_info.id, fsl::GetKoid(display_info.display_ref().reference.get()),
      hardware_display_info.pixel_format, std::move(controller), std::move(display_info)};
}

void DisplayManager2::OnDisplaysChanged(
    DisplayControllerPrivate* dc_private,
    std::vector<fuchsia::hardware::display::Info> displays_added,
    std::vector<uint64_t> displays_removed) {
  for (fuchsia::hardware::display::Info& display_info : displays_added) {
    if (HasDisplayWithId(dc_private->displays, display_info.id)) {
      last_error_ = "DisplayManager: Display added, but a display already exists with same id=" +
                    std::to_string(display_info.id);
      FXL_LOG(WARNING) << last_error_;
      continue;
    }
    if (dc_private->claimed_dc) {
      dc_private->claimed_dc->AddDisplay(
          Display2(display_info.id, display_info.modes, display_info.pixel_format));
    }
    dc_private->displays.push_back(NewDisplayInfoPrivate(display_info, dc_private->controller));
    const DisplayInfoPrivate& display_info_private = dc_private->displays.back();
    for (auto& listener : display_listeners_.ptrs()) {
      InvokeDisplayAddedForListener(*listener, display_info_private);
    }
  }

  for (uint64_t display_id : displays_removed) {
    std::optional<DisplayInfoPrivate> display_info_private =
        RemoveDisplayWithId(&dc_private->displays, display_id);

    if (!display_info_private) {
      last_error_ = "DisplayManager: Got a display removed event for invalid display=" +
                    std::to_string(display_id);
      FXL_LOG(WARNING) << last_error_;
      continue;
    }

    if (dc_private->claimed_dc) {
      bool deleted = dc_private->claimed_dc->RemoveDisplay(display_id);
      if (!deleted) {
        last_error_ = "DisplayManager: Unable to remove display for display controller, id=" +
                      std::to_string(display_id);
        FXL_LOG(WARNING) << last_error_;
        continue;
      }
    }

    fuchsia::ui::display::DisplayRef display_ref =
        fidl::Clone(display_info_private->info.display_ref());
    for (auto& listener : display_listeners_.ptrs()) {
      (*listener)->OnDisplayRemoved(DuplicateDisplayRef(display_ref));
    }
  }
}

void DisplayManager2::OnDisplayOwnershipChanged(DisplayControllerPrivate* dc, bool has_ownership) {
  dc->has_ownership = has_ownership;

  if (dc->displays.empty()) {
    return;
  }

  for (auto& listener : display_listeners_.ptrs()) {
    InvokeDisplayOwnershipChangedForListener(*listener, dc, has_ownership);
  }
}

void DisplayManager2::RemoveOnInvalid(DisplayControllerPrivate* dc) {
  auto it = std::find_if(display_controllers_private_.begin(), display_controllers_private_.end(),
                         [dc](DisplayControllerPrivateUniquePtr& p) { return p.get() == dc; });
  FXL_DCHECK(it != display_controllers_private_.end());
  auto display_controller = std::move(*it);
  display_controllers_private_.erase(it);
  for (auto& display : display_controller->displays) {
    for (auto& listener : display_listeners_.ptrs()) {
      (*listener)->OnDisplayRemoved(DuplicateDisplayRef(display.info.display_ref()));
    }
  }
}

}  // namespace display
}  // namespace scenic_impl
