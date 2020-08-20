// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/a11y_view_semantics.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/zx/event.h>

#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree_service_factory.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace accessibility_test {
namespace {

class ViewSemanticsTest : public gtest::TestLoopFixture {
 public:
  ViewSemanticsTest() {}
  ~ViewSemanticsTest() override = default;

  void SetUp() override {
    TestLoopFixture::SetUp();

    semantic_tree_service_factory_ = std::make_unique<MockSemanticTreeServiceFactory>();

    mock_semantic_listener_ = std::make_unique<MockSemanticListener>();
    semantic_listener_binding_ =
        std::make_unique<fidl::Binding<fuchsia::accessibility::semantics::SemanticListener>>(
            mock_semantic_listener_.get());

    koid_ = a11y::GetKoid(view_ref_);

    fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener_ptr;
    auto tree_service = semantic_tree_service_factory_->NewService(
        koid_, std::move(semantic_listener_ptr),
        context_provider_.context()->outgoing()->debug_dir(), [](zx_status_t status) {});
    tree_service_ = tree_service.get();

    view_semantics_ =
        std::make_unique<a11y::A11yViewSemantics>(std::move(tree_service), tree_ptr_.NewRequest());
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSemanticTreeServiceFactory> semantic_tree_service_factory_;
  std::unique_ptr<MockSemanticListener> mock_semantic_listener_;
  std::unique_ptr<fidl::Binding<fuchsia::accessibility::semantics::SemanticListener>>
      semantic_listener_binding_;
  std::unique_ptr<a11y::A11yViewSemantics> view_semantics_;
  a11y::SemanticTreeService* tree_service_;
  fuchsia::accessibility::semantics::SemanticTreePtr tree_ptr_;
  fuchsia::ui::views::ViewRef view_ref_;
  zx_koid_t koid_;
};

TEST_F(ViewSemanticsTest, TestEnableSemantics) {
  view_semantics_->EnableSemanticUpdates(true);

  EXPECT_TRUE(semantic_tree_service_factory_->service());
  EXPECT_TRUE(semantic_tree_service_factory_->service()->UpdatesEnabled());
}

}  // namespace
}  // namespace accessibility_test
