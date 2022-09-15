// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/tests/mocks/mock_view_source.h"

#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {

fxl::WeakPtr<a11y::ViewWrapper> MockViewSource::GetViewWrapper(zx_koid_t koid) {
  auto it = views_.find(koid);

  if (it == views_.end()) {
    return nullptr;
  }

  return it->second->GetWeakPtr();
}

void MockViewSource::CreateView(const ViewRefHelper& view_ref) {
  FX_CHECK(views_.count(view_ref.koid()) == 0);

  views_[view_ref.koid()] = std::make_unique<a11y::ViewWrapper>(
      view_ref.Clone(), std::make_unique<MockViewSemantics>(),
      std::make_unique<MockAnnotationView>([]() {}, []() {}, []() {}));
}

}  // namespace accessibility_test
