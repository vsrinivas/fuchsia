// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_VIEW_SOURCE_H_
#define SRC_UI_A11Y_LIB_VIEW_VIEW_SOURCE_H_

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/view/view_wrapper.h"

namespace a11y {

// Interface that owns a mapping between a view's ViewRef KOID and its
// associated resources/capabilities.
class ViewSource {
 public:
  ViewSource() = default;
  virtual ~ViewSource() = default;

  // Returns a weak pointer to the ViewWrapper object that corresponds to
  // `view_ref_koid`.
  //
  // Returns nullptr if no ViewWrapper is found for the given KOID.
  virtual fxl::WeakPtr<ViewWrapper> GetViewWrapper(zx_koid_t view_ref_koid) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_VIEW_SOURCE_H_
