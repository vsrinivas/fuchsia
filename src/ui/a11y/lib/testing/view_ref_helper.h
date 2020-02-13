// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_TESTING_VIEW_REF_HELPER_H_
#define SRC_UI_A11Y_LIB_TESTING_VIEW_REF_HELPER_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <zircon/types.h>

namespace accessibility_test {

// A helper class to manage ViewRefs in accessibility tests.
//
// When it is initiated, it creates a ViewRef which can be later cloned / signaled.
class ViewRefHelper {
 public:
  ViewRefHelper();
  ~ViewRefHelper() = default;

  // Returns the koid of the ViewRef owned by this class.
  zx_koid_t koid() const;

  // Resets the ViewRef by sending a signal on the ViewRef's event pair.
  void SendEventPairSignal();

  // Returns a clone of the ViewRef owned by this class.
  fuchsia::ui::views::ViewRef Clone() const;

 private:
  fuchsia::ui::views::ViewRef view_ref_;

  // The event signaling pair member, used to invalidate the View Ref.
  zx::eventpair eventpair_peer_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_TESTING_VIEW_REF_HELPER_H_
