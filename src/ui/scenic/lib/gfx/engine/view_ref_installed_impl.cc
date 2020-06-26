// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/view_ref_installed_impl.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/rights.h>

#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl::gfx {

using fuchsia::ui::views::ViewRefInstalled;

namespace {

// Check if a ViewRef has the correct rights.
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

  const zx_koid_t view_ref_koid = utils::ExtractKoid(view_ref);
  if (is_installed_(view_ref_koid)) {
    callback(InstalledMessage());
    return;
  }

  // Not invalid, not installed. Save callback until installation or invalidation.
  pending_callbacks_[view_ref_koid].emplace_back(std::move(callback));

  // Handle state that only needs one instance per ViewRef.
  if (invalidation_waiters_.find(view_ref_koid) == invalidation_waiters_.end()) {
    // When the event is invalidated, send error message and clean up.
    auto [waiter_it, success] = invalidation_waiters_.emplace(view_ref_koid, std::move(view_ref));
    FX_DCHECK(success);
    auto& waiter = waiter_it->second.waiter;
    zx_status_t status =
        waiter.Begin(async_get_default_dispatcher(),
                     std::bind(&ViewRefInstalledImpl::OnViewRefInvalidated, this, view_ref_koid,
                               std::placeholders::_3, std::placeholders::_4));
    FX_DCHECK(status == ZX_OK);
  }
}

void ViewRefInstalledImpl::OnViewRefInstalled(zx_koid_t view_ref_koid) {
  const auto it = pending_callbacks_.find(view_ref_koid);
  if (it == pending_callbacks_.end()) {
    return;
  }

  for (auto& callback : it->second) {
    callback(InstalledMessage());
  }
  CleanUp(view_ref_koid);
}

void ViewRefInstalledImpl::OnViewRefInvalidated(zx_koid_t view_ref_koid, zx_status_t status,
                                                const zx_packet_signal* signal) {
  if (status != ZX_OK) {
    FX_LOGS(WARNING)
        << "ViewRefInstalledImpl received an error status code on viewref invalidation: " << status;
  }

  for (auto& callback : pending_callbacks_.at(view_ref_koid)) {
    callback(InvalidMessage());
  }
  CleanUp(view_ref_koid);
}

void ViewRefInstalledImpl::CleanUp(zx_koid_t view_ref_koid) {
  pending_callbacks_.erase(view_ref_koid);
  invalidation_waiters_.erase(view_ref_koid);
}

}  // namespace scenic_impl::gfx
