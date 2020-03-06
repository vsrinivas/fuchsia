// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_SOURCE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_SOURCE_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <zircon/types.h>

#include <optional>

namespace a11y {

// An interface for a11y query existing semantic information.
// TODO(fxb/46164): Move all semantic consuming methods from View manager to this interface.
class SemanticsSource {
 public:
  SemanticsSource() = default;
  virtual ~SemanticsSource() = default;

  // Returns true if the view referenced by |view_ref_koid| is providing semantics.
  virtual bool ViewHasSemantics(zx_koid_t view_ref_koid) = 0;

  // Returns a clone of the ViewRef referenced by |view_ref_koid| if it is known.
  // TODO(fxb/47136): Move ViewRefClone from SemanticsSource to ViewRefWrapper.
  virtual std::optional<fuchsia::ui::views::ViewRef> ViewRefClone(zx_koid_t view_ref_koid) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_SOURCE_H_
