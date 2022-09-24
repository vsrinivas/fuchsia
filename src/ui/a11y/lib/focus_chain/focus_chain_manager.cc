// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/focus_chain/focus_chain_manager.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

FocusChainManager::FocusChainManager(std::shared_ptr<AccessibilityViewInterface> a11y_view)
    : a11y_view_(a11y_view) {
  FX_DCHECK(a11y_view_);
}

FocusChainManager::ViewRefWatcher::ViewRefWatcher(fuchsia::ui::views::ViewRef view_ref,
                                                  OnViewRefInvalidatedCallback callback)
    : view_ref_(std::move(view_ref)),
      callback_(std::move(callback)),
      wait_(this, view_ref_.reference.get(), ZX_EVENTPAIR_PEER_CLOSED) {
  FX_CHECK(wait_.Begin(async_get_default_dispatcher()) == ZX_OK);
}

FocusChainManager::ViewRefWatcher::~ViewRefWatcher() = default;

void FocusChainManager::OnFocusChange(fuchsia::ui::focus::FocusChain focus_chain,
                                      OnFocusChangeCallback callback) {
  focus_chain_.clear();
  if (focus_chain.has_focus_chain()) {
    auto& new_focus_chain = *(focus_chain.mutable_focus_chain());
    for (auto& view_ref : new_focus_chain) {
      auto view_ref_watcher = std::make_unique<ViewRefWatcher>(
          std::move(view_ref), [this]() { InvalidateFocusChain(); });
      focus_chain_.push_back(std::move(view_ref_watcher));
    }
  }
  Notify();
  callback();
}

void FocusChainManager::Register(fxl::WeakPtr<AccessibilityFocusChainListener> listener) {
  // On registration, sends the listener the current focus.
  if (listener) {
    listener->OnViewFocus(GetFocusedView());
    listeners_.push_back(std::move(listener));
  }
}

void FocusChainManager::InvalidateFocusChain() {
  focus_chain_.clear();
  Notify();
}

zx_koid_t FocusChainManager::GetFocusedView() const {
  if (focus_chain_.empty()) {
    return ZX_KOID_INVALID;
  }
  const auto& focused_view = focus_chain_.back();
  return focused_view->koid();
}

void FocusChainManager::Notify() {
  zx_koid_t focused_view = GetFocusedView();
  auto it = listeners_.begin();
  while (it != listeners_.end()) {
    if (*it) {
      (*it)->OnViewFocus(focused_view);
      ++it;
    } else {
      // This listener is no longer listening for updates.
      it = listeners_.erase(it);
    }
  }
}

zx_koid_t FocusChainManager::ViewRefWatcher::koid() const { return GetKoid(view_ref_); }

void FocusChainManager::ViewRefWatcher::OnViewRefSignaled(async_dispatcher_t* dispatcher,
                                                          async::WaitBase* wait, zx_status_t status,
                                                          const zx_packet_signal* signal) {
  // The only signal we are waiting for is ZX_EVENTPAIR_PEER_CLOSED.
  callback_();
}

void FocusChainManager::ChangeFocusToView(fuchsia::ui::views::ViewRef view_ref,
                                          ChangeFocusToViewCallback callback) {
  a11y_view_->RequestFocus(std::move(view_ref), [callback = std::move(callback)](auto result) {
    if (result.is_err()) {
      callback(false);
    } else {
      callback(true);
    }
  });
  return;
}

}  // namespace a11y
