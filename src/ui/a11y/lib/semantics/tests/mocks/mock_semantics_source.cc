// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"

#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

void MockSemanticsSource::AddViewRef(fuchsia::ui::views::ViewRef view_ref) {
  view_ref_ = std::move(view_ref);
}

bool MockSemanticsSource::ViewHasSemantics(zx_koid_t view_ref_koid) {
  return view_ref_koid == a11y::GetKoid(view_ref_);
}

std::optional<fuchsia::ui::views::ViewRef> MockSemanticsSource::ViewRefClone(
    zx_koid_t view_ref_koid) {
  if (!ViewHasSemantics(view_ref_koid)) {
    return std::nullopt;
  }
  return a11y::Clone(view_ref_);
}

}  // namespace accessibility_test
