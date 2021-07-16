// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_FOCUS_CHAIN_VIEW_FOCUSER_H_
#define SRC_UI_A11Y_LIB_FOCUS_CHAIN_VIEW_FOCUSER_H_

#include <lib/fit/function.h>
#include <zircon/types.h>

namespace a11y {

// Interface for the accessibility manager to request focus changes.
class ViewFocuserInterface {
 public:
  using RequestFocusCallback = fit::function<void(bool)>;

  ViewFocuserInterface() = default;
  virtual ~ViewFocuserInterface() = default;

  // Attempts to transfer focus to the view corresponding to |koid|.
  // Callback is invoked with argument |true| if the request succeeded and
  // |false| otherwise.
  virtual void RequestFocus(zx_koid_t koid, RequestFocusCallback callback) = 0;
};

}  //  namespace a11y

#endif  // SRC_UI_A11Y_LIB_FOCUS_CHAIN_VIEW_FOCUSER_H_
