// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_FOCUS_LISTENER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_FOCUS_LISTENER_H_

#include <fuchsia/ui/views/cpp/fidl.h>

namespace root_presenter {

// The interface between FocusDispatcher and in-process objects which wish to
// receive focus updates.
class FocusListener {
 public:
  // Informs the callee that `focused_view` now has focus.
  //
  // The caller _should_ always provide a valid ViewRef. In particular,
  // the caller should ensure that
  //    `focused_view.reference.get() != ZX_HANDLE_INVALID`
  virtual void NotifyFocusChange(fuchsia::ui::views::ViewRef focused_view) = 0;
};
}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_FOCUS_LISTENER_H_
