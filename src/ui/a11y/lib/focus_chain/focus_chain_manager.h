// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_FOCUS_CHAIN_FOCUS_CHAIN_MANAGER_H_
#define SRC_UI_A11Y_LIB_FOCUS_CHAIN_FOCUS_CHAIN_MANAGER_H_

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <lib/async/cpp/wait.h>

#include <memory>
#include <vector>

#include "src/ui/a11y/lib/focus_chain/accessibility_focus_chain_listener.h"
#include "src/ui/a11y/lib/focus_chain/accessibility_focus_chain_requester.h"
#include "src/ui/a11y/lib/semantics/semantics_source.h"

namespace a11y {

// The Focus Chain manager processes Focus Chain Updates and dispatches to
// registered a11y services the views that are currently in focus.
//
// This manager also can request Focus Chain Updates. It exposes
// |AccessibilityFocusChainRequester| interface, which accessibility services
// can use to change the Focus Chain to a different view.
class FocusChainManager : public fuchsia::ui::focus::FocusChainListener,
                          public AccessibilityFocusChainRegistry,
                          public AccessibilityFocusChainRequester {
 public:
  // |focuser| is the client-side channel interface used to request Focus Chain Updates.
  // |semantics_source| object must outlive FocusChainManager.
  FocusChainManager(fuchsia::ui::views::FocuserPtr focuser, SemanticsSource* semantics_source);
  ~FocusChainManager() = default;

  // |fuchsia.ui.focus.FocusChainListener|
  void OnFocusChange(fuchsia::ui::focus::FocusChain focus_chain,
                     OnFocusChangeCallback callback) override;

  // |AccessibilityFocusChainRegistry|
  void Register(fxl::WeakPtr<AccessibilityFocusChainListener> listener) override;

  // |AccessibilityFocusChainRequester|
  void ChangeFocusToView(zx_koid_t view_ref_koid, ChangeFocusToViewCallback callback) override;

 private:
  // A ViewRefWatcher holds a ViewRef and watches for any signaling on the ViewRef.
  class ViewRefWatcher {
   public:
    using OnViewRefInvalidatedCallback = fit::function<void()>;
    ViewRefWatcher(fuchsia::ui::views::ViewRef view_ref, OnViewRefInvalidatedCallback callback);
    ~ViewRefWatcher();
    zx_koid_t koid() const;

   private:
    // |async::WaitMethod|
    // When called, |callback_| is invoked.
    void OnViewRefSignaled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                           zx_status_t status, const zx_packet_signal* signal);

    fuchsia::ui::views::ViewRef view_ref_;
    OnViewRefInvalidatedCallback callback_;
    async::WaitMethod<ViewRefWatcher, &ViewRefWatcher::OnViewRefSignaled> wait_;
  };

  // Invalidates the Focus Chain owned by this manager. This causes |Notify()| to be invoked,
  // informing every registered listener to the focus being updated.
  void InvalidateFocusChain();

  // Returns the ViewRef KoID of the view that has the focus in this Focus Chain.
  // If no view is in focus, returns ZX_KOID_INVALID.
  zx_koid_t GetFocusedView() const;

  // Notifies all registered listeners of the new view in focus.
  void Notify();

  // Registered listeners with this manager.
  std::vector<fxl::WeakPtr<AccessibilityFocusChainListener>> listeners_;
  // Note that ViewRefWatcher can't have a move constructor due to
  // async::WaitMethod. Thus, this object is created only once and later moved
  // via an unique_ptr.
  std::vector<std::unique_ptr<ViewRefWatcher>> focus_chain_;

  // Responsible for requesting Focus Chain updates.
  fuchsia::ui::views::FocuserPtr focuser_;

  // Right now, this manager only responds to Focus Chain Requests to Views that
  // are providing semantics. It uses |semantics_source_| to validate if a
  // view is providing semantics before doing the request.
  SemanticsSource* const semantics_source_ = nullptr;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_FOCUS_CHAIN_FOCUS_CHAIN_MANAGER_H_
