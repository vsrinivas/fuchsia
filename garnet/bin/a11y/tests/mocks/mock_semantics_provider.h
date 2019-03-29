// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SEMANTICS_PROVIDER_H_
#define GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SEMANTICS_PROVIDER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace accessibility_test {

class MockSemanticsProvider : public fuchsia::accessibility::SemanticsProvider {
 public:
  // On initialization, MockSemanticsProvider tries to connect to
  // |fuchsia::accessibility::SemanticsRoot| service in |context_| and
  // registers with its |view_id_| and |binding|.
  MockSemanticsProvider(sys::ComponentContext* context, zx_koid_t view_id);
  ~MockSemanticsProvider() = default;

  // These functions directly call the |fuchsia::accessibility::SemanticsRoot|
  // functions for this semantics provider.
  void UpdateSemanticsNodes(
      fidl::VectorPtr<fuchsia::accessibility::Node> update_nodes);
  void DeleteSemanticsNodes(fidl::VectorPtr<int32_t> delete_nodes);
  void Commit();

 private:
  void PerformAccessibilityAction(
      int32_t node_id, fuchsia::accessibility::Action action) override {}

  fidl::Binding<fuchsia::accessibility::SemanticsProvider> binding_;
  sys::ComponentContext* context_;
  fuchsia::accessibility::SemanticsRootPtr root_;
  zx_koid_t view_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockSemanticsProvider);
};

}  // namespace accessibility_test

#endif  // GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SEMANTICS_PROVIDER_H_