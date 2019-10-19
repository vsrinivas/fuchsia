// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_manager2.h"

#include <lib/fidl/cpp/clone.h>

#include <iterator>
#include <string>

#include "src/lib/fxl/logging.h"

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

DisplayManager2::DisplayControllerHolder::DisplayControllerHolder(
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller,
    std::unique_ptr<DisplayControllerListener> listener, std::vector<DisplayInfoHolder> displays,
    bool has_ownership)
    : controller_(controller),
      listener_(std::move(listener)),
      displays_(std::move(displays)),
      has_ownership_(has_ownership) {}

DisplayManager2::DisplayControllerHolder::~DisplayControllerHolder() {
  // Explicitly clear callbacks immediately before destruction, since the callbacks
  // capture a pointer to |this|.
  listener_->ClearCallbacks();
}

void DisplayManager2::DisplayControllerHolder::AddDisplay(DisplayInfoHolder display) {
  displays_.push_back(std::move(display));
}

bool DisplayManager2::DisplayControllerHolder::HasDisplayWithId(uint64_t display_id) {
  auto display_it = std::find_if(
      displays_.begin(), displays_.end(),
      [display_id](DisplayInfoHolder& display_info) { return display_info.id == display_id; });
  return display_it != displays_.end();
}

std::optional<DisplayManager2::DisplayInfoHolder>
DisplayManager2::DisplayControllerHolder::RemoveDisplayWithId(uint64_t display_id) {
  auto display_it = std::find_if(
      displays_.begin(), displays_.end(),
      [display_id](DisplayInfoHolder& display_info) { return display_info.id == display_id; });
  if (display_it != displays_.end()) {
    auto removed_display = std::make_optional<DisplayInfoHolder>(std::move(*display_it));
    displays_.erase(display_it);
    return removed_display;
  } else {
    return std::optional<DisplayInfoHolder>();
  }
}

DisplayManager2::DisplayManager2() {}

void DisplayManager2::AddDisplayController(
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller,
    std::unique_ptr<DisplayControllerListener> controller_listener) {
  auto display_controller_holder = std::make_unique<DisplayControllerHolder>(
      std::move(controller), std::move(controller_listener),
      /*displays=*/std::vector<DisplayInfoHolder>(),
      /*has_ownership=*/false);

  DisplayControllerHolder* dc = display_controller_holder.get();

  display_controllers_.push_back(std::move(display_controller_holder));

  auto on_invalid_cb = [this, dc]() { RemoveOnInvalid(dc); };

  auto displays_changed_cb = [this, dc](std::vector<fuchsia::hardware::display::Info> added,
                                        std::vector<uint64_t> removed) {
    OnDisplaysChanged(dc, std::move(added), std::move(removed));
  };

  auto display_ownership_changed_cb = [this, dc](bool has_ownership) {
    OnDisplayOwnershipChanged(dc, has_ownership);
  };

  // Note: These callbacks are all cleared in the destructor for |dc|.
  dc->listener()->InitializeCallbacks(std::move(on_invalid_cb), std::move(displays_changed_cb),
                                      std::move(display_ownership_changed_cb));
}

void DisplayManager2::InvokeDisplayAddedForListener(
    const fidl::InterfacePtr<fuchsia::ui::display::DisplayListener>& listener,
    const DisplayInfoHolder& display_info_holder) {
  auto display_info_clone = fidl::Clone(display_info_holder.info);
  display_info_clone.set_display_ref(DuplicateDisplayRef(display_info_clone.display_ref()));
  listener->OnDisplayAdded(std::move(display_info_clone));
}

void DisplayManager2::InvokeDisplayOwnershipChangedForListener(
    const fidl::InterfacePtr<fuchsia::ui::display::DisplayListener>& listener,
    DisplayControllerHolder* dc, bool has_ownership) {
  if (dc->displays().empty()) {
    return;
  }
  std::vector<fuchsia::ui::display::DisplayRef> display_refs;
  for (auto& display_info_holder : dc->displays()) {
    display_refs.push_back(DuplicateDisplayRef(display_info_holder.info.display_ref()));
  }
  listener->OnDisplayOwnershipChanged(std::move(display_refs), has_ownership);
}

void DisplayManager2::AddDisplayListener(
    fidl::InterfaceHandle<fuchsia::ui::display::DisplayListener>
        display_listener_interface_handle) {
  fuchsia::ui::display::DisplayListenerPtr display_listener =
      display_listener_interface_handle.Bind();

  for (auto& display_controller : display_controllers_) {
    // Deliver OnDisplayAdded events for all the displays that currently exist.
    for (auto& display : display_controller->displays()) {
      InvokeDisplayAddedForListener(display_listener, display);
    }

    // Notify the client if we have ownership of the display controller.
    bool has_ownership = display_controller->has_ownership();
    if (has_ownership) {
      InvokeDisplayOwnershipChangedForListener(display_listener, display_controller.get(),
                                               has_ownership);
    }
  }

  display_listeners_.AddInterfacePtr(std::move(display_listener));
}

DisplayManager2::DisplayInfoHolder DisplayManager2::NewDisplayInfoHolder(
    fuchsia::hardware::display::Info hardware_display_info,
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> controller) {
  fuchsia::ui::display::Info display_info;
  display_info.set_display_ref(NewDisplayRef());
  display_info.set_modes(std::move(hardware_display_info.modes));
  display_info.set_manufacturer_name(hardware_display_info.manufacturer_name);
  display_info.set_monitor_name(hardware_display_info.monitor_name);

  return DisplayInfoHolder{hardware_display_info.id, std::move(controller),
                           std::move(display_info)};
}

void DisplayManager2::OnDisplaysChanged(
    DisplayControllerHolder* dc, std::vector<fuchsia::hardware::display::Info> displays_added,
    std::vector<uint64_t> displays_removed) {
  for (fuchsia::hardware::display::Info& display : displays_added) {
    if (dc->HasDisplayWithId(display.id)) {
      last_error_ = "DisplayManager: Display added, but a display already exists with same id=" +
                    std::to_string(display.id);
      FXL_LOG(WARNING) << last_error_;
      continue;
    }
    dc->AddDisplay(NewDisplayInfoHolder(display, dc->controller()));
    const DisplayInfoHolder& display_info_holder = dc->displays().back();
    for (auto& listener : display_listeners_.ptrs()) {
      InvokeDisplayAddedForListener(*listener, display_info_holder);
    }
  }

  for (uint64_t display_id : displays_removed) {
    std::optional<DisplayInfoHolder> display_info_opt = dc->RemoveDisplayWithId(display_id);

    if (!display_info_opt) {
      last_error_ = "DisplayManager: Got a display removed event for invalid display=" +
                    std::to_string(display_id);
      FXL_LOG(WARNING) << last_error_;
      continue;
    }

    fuchsia::ui::display::DisplayRef display_ref =
        fidl::Clone(display_info_opt->info.display_ref());
    for (auto& listener : display_listeners_.ptrs()) {
      (*listener)->OnDisplayRemoved(DuplicateDisplayRef(display_ref));
    }
  }
}

void DisplayManager2::OnDisplayOwnershipChanged(DisplayControllerHolder* dc, bool has_ownership) {
  dc->set_has_ownership(has_ownership);

  if (dc->displays().empty()) {
    return;
  }

  for (auto& listener : display_listeners_.ptrs()) {
    InvokeDisplayOwnershipChangedForListener(*listener, dc, has_ownership);
  }
}

void DisplayManager2::RemoveOnInvalid(DisplayControllerHolder* dc) {
  auto it =
      std::find_if(display_controllers_.begin(), display_controllers_.end(),
                   [dc](std::unique_ptr<DisplayControllerHolder>& p) { return p.get() == dc; });
  FXL_DCHECK(it != display_controllers_.end());
  auto display_controller = std::move(*it);
  display_controllers_.erase(it);
  for (auto& display : display_controller->displays()) {
    for (auto& listener : display_listeners_.ptrs()) {
      (*listener)->OnDisplayRemoved(DuplicateDisplayRef(display.info.display_ref()));
    }
  }
}

}  // namespace display
}  // namespace scenic_impl
