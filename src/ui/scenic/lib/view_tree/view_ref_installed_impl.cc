// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/view_ref_installed_impl.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/rights.h>

#include "src/ui/scenic/lib/utils/helpers.h"

namespace view_tree {

using fuchsia::ui::views::ViewRefInstalled;

namespace {

// Check if a ViewRef is valid and has the correct rights.
bool IsValidViewRef(fuchsia::ui::views::ViewRef& view_ref) {
  if (view_ref.reference.get_info(ZX_INFO_HANDLE_VALID, nullptr, /*buffer size*/ 0, nullptr,
                                  nullptr) != ZX_OK) {
    FX_LOGS(INFO) << "Bad handle";
    return false;  // bad handle
  }

  zx_info_handle_basic_t info{};
  if (view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &info, /*buffer size*/ sizeof(info),
                                  nullptr, nullptr) != ZX_OK) {
    FX_LOGS(INFO) << "No info";
    return false;  // no info
  }

  if (!(info.rights & ZX_RIGHT_WAIT)) {
    FX_LOGS(INFO) << "Bad rights";
    return false;  // unexpected rights
  }

  return true;
}

fuchsia::ui::views::ViewRefInstalled_Watch_Result InvalidMessage() {
  return fuchsia::ui::views::ViewRefInstalled_Watch_Result::WithErr(
      fuchsia::ui::views::ViewRefInstalledError::INVALID_VIEW_REF);
}

fuchsia::ui::views::ViewRefInstalled_Watch_Result InstalledMessage() {
  return fuchsia::ui::views::ViewRefInstalled_Watch_Result::WithResponse({});
}

}  // namespace

void ViewRefInstalledImpl::Publish(sys::ComponentContext* app_context) {
  FX_DCHECK(app_context);
  app_context->outgoing()->AddPublicService<ViewRefInstalled>(bindings_.GetHandler(this));
}

// |ViewRefInstalled|
void ViewRefInstalledImpl::Watch(fuchsia::ui::views::ViewRef view_ref,
                                 ViewRefInstalled::WatchCallback callback) {
  if (!IsValidViewRef(view_ref)) {
    callback(InvalidMessage());
    return;
  }

  // Check if already installed.
  const zx_koid_t view_ref_koid = utils::ExtractKoid(view_ref);
  if (installed_views_.count(view_ref_koid) != 0) {
    callback(InstalledMessage());
    return;
  }

  // Not invalid, not installed.
  if (watched_views_.count(view_ref_koid) == 0) {
    // If it doesn't exist: add a new entry and setup the invalidation waiter.
    auto [it, success] = watched_views_.emplace(view_ref_koid, std::move(view_ref));
    FX_DCHECK(success);

    // When the event is invalidated, send error message and clean up.
    const zx_status_t status =
        watched_views_.at(view_ref_koid)
            .invalidation_waiter.waiter.Begin(
                async_get_default_dispatcher(),
                std::bind(&ViewRefInstalledImpl::OnViewRefInvalidated, this, view_ref_koid,
                          std::placeholders::_3, std::placeholders::_4));
    FX_DCHECK(status == ZX_OK);
  }
  // Save callback until installation or invalidation
  watched_views_.at(view_ref_koid).callbacks.emplace_back(std::move(callback));
}

void ViewRefInstalledImpl::OnNewViewTreeSnapshot(std::shared_ptr<const Snapshot> snapshot) {
  // Remove any stale views from the installed_views_ set.
  for (auto it = installed_views_.begin(); it != installed_views_.end();) {
    if (snapshot->view_tree.count(*it) == 0 && snapshot->unconnected_views.count(*it) == 0) {
      it = installed_views_.erase(it);
    } else {
      ++it;
    }
  }

  // Update any newly installed views.
  for (const auto& [koid, _] : snapshot->view_tree) {
    const auto [__, success] = installed_views_.emplace(koid);
    if (success) {
      OnViewRefInstalled(koid);
    }
  }
}

void ViewRefInstalledImpl::OnViewRefInstalled(zx_koid_t view_ref_koid) {
  const auto it = watched_views_.find(view_ref_koid);
  if (it == watched_views_.end()) {
    return;
  }

  for (auto& callback : it->second.callbacks) {
    callback(InstalledMessage());
  }
  watched_views_.erase(view_ref_koid);
}

void ViewRefInstalledImpl::OnViewRefInvalidated(zx_koid_t view_ref_koid, zx_status_t status,
                                                const zx_packet_signal* signal) {
  if (status != ZX_OK) {
    FX_LOGS(WARNING)
        << "ViewRefInstalledImpl received an error status code on viewref invalidation: " << status;
  }

  // No need to check for existence. OnViewRefInvalidated is only called from invalidation_waiter.
  for (auto& callback : watched_views_.at(view_ref_koid).callbacks) {
    callback(InvalidMessage());
  }
  watched_views_.erase(view_ref_koid);
}

}  // namespace view_tree
