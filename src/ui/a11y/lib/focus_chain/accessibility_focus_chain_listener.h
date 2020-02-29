// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_FOCUS_CHAIN_ACCESSIBILITY_FOCUS_CHAIN_LISTENER_H_
#define SRC_UI_A11Y_LIB_FOCUS_CHAIN_ACCESSIBILITY_FOCUS_CHAIN_LISTENER_H_

#include "src/lib/fxl/memory/weak_ptr.h"

namespace a11y {

// An interface for A11Y listen for Focus Chain updates.
// A listener registers itself into a
// AccessibilityFocusChainRegistry::Register(), and receives updates via calls
// to OnViewFocus().
class AccessibilityFocusChainListener {
 public:
  AccessibilityFocusChainListener() = default;
  virtual ~AccessibilityFocusChainListener() = default;

  // This method gets called when there is an Focus Chain update. If no view is
  // in focus, ZX_KOID_INVALID is sent.
  virtual void OnViewFocus(zx_koid_t view_ref_koid) = 0;
};

// A registry interface to add accessibility listeners of Focus Chain updates.
class AccessibilityFocusChainRegistry {
 public:
  AccessibilityFocusChainRegistry() = default;
  virtual ~AccessibilityFocusChainRegistry() = default;

  // Registers a new listener with this registry. As long as the WeakPtr passed
  // is valid, the listener still gets updates. Once the WeakPtr has been
  // invalidated, the listener gets removed from this registry.
  virtual void Register(fxl::WeakPtr<AccessibilityFocusChainListener> listener) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_FOCUS_CHAIN_ACCESSIBILITY_FOCUS_CHAIN_LISTENER_H_
