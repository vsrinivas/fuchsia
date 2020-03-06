// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_SOURCE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_SOURCE_H_

#include <fuchsia/ui/views/cpp/fidl.h>

#include <optional>

#include "src/ui/a11y/lib/semantics/semantics_source.h"

namespace accessibility_test {

class MockSemanticsSource : public a11y::SemanticsSource {
 public:
  MockSemanticsSource() = default;
  ~MockSemanticsSource() = default;

  // Adds a ViewRef to be owned by this mock. Calls to ViewHasSemantics() and ViewRefClone() will
  // respond to this ViewRef accordingly.
  void AddViewRef(fuchsia::ui::views::ViewRef view_ref);

  // |SemanticsSource|
  bool ViewHasSemantics(zx_koid_t view_ref_koid) override;

  // |SemanticsSource|
  std::optional<fuchsia::ui::views::ViewRef> ViewRefClone(zx_koid_t view_ref_koid) override;

 private:
  fuchsia::ui::views::ViewRef view_ref_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_SOURCE_H_
