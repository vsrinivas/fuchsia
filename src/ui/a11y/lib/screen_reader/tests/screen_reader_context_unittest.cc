// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"
#include "src/ui/a11y/lib/testing/view_ref_helper.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_source.h"

namespace accessibility_test {
namespace {

class ScreenReaderContextTest : public gtest::RealLoopFixture {
 public:
  ScreenReaderContextTest() : context_provider_(), tts_manager_(context_provider_.context()) {
    // Initialize A11yFocusManager.
    auto a11y_focus_manager = std::make_unique<MockA11yFocusManager>();

    // Store raw pointer to A11yFocusManager.
    a11y_focus_manager_ptr_ = a11y_focus_manager.get();

    // Initialize screen reader context.
    screen_reader_context_ = std::make_unique<a11y::ScreenReaderContext>(
        std::move(a11y_focus_manager), &tts_manager_, &mock_view_source_);

    // Create a mock view.
    mock_view_source_.CreateView(view_ref_helper_);
  }

  void CreateOrUpdateSemanticNode(fuchsia::accessibility::semantics::Node node) {
    std::vector<a11y::SemanticTree::TreeUpdate> node_updates;
    node_updates.push_back(std::move(node));
    mock_view_source_.UpdateSemanticTree(view_ref_helper_.koid(), std::move(node_updates));
  }

  sys::testing::ComponentContextProvider context_provider_;
  MockAccessibilityFocusChainRequester mock_focus_requester_;
  MockAccessibilityFocusChainRegistry mock_focus_registry_;
  MockViewSource mock_view_source_;
  MockA11yFocusManager* a11y_focus_manager_ptr_ = nullptr;
  a11y::TtsManager tts_manager_;
  std::unique_ptr<a11y::ScreenReaderContext> screen_reader_context_;
  ViewRefHelper view_ref_helper_;
};

// Checks that the pointer returned by GetA11yFocusManager matches the one passed in the
// constructor.
TEST_F(ScreenReaderContextTest, GetA11yFocusManager) {
  ASSERT_EQ(a11y_focus_manager_ptr_, screen_reader_context_->GetA11yFocusManager());
}

TEST_F(ScreenReaderContextTest, ContainsLocaleId) {
  EXPECT_EQ(screen_reader_context_->locale_id(), "en-US");
  screen_reader_context_->set_locale_id("foo-bar");
  EXPECT_EQ(screen_reader_context_->locale_id(), "foo-bar");
}

// Makes sure that the Speaker is instantiated in when the context is created.
TEST_F(ScreenReaderContextTest, GetSpeaker) { ASSERT_TRUE(screen_reader_context_->speaker()); }

TEST_F(ScreenReaderContextTest, SetsSemanticLevel) {
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            a11y::ScreenReaderContext::SemanticLevel::kDefault);
  screen_reader_context_->set_semantic_level(a11y::ScreenReaderContext::SemanticLevel::kWord);
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            a11y::ScreenReaderContext::SemanticLevel::kWord);
}

TEST_F(ScreenReaderContextTest, IsVirtualKeyboardFocused) {
  a11y_focus_manager_ptr_->SetA11yFocus(view_ref_helper_.koid(), 0u, [](auto...) {});
  fuchsia::accessibility::semantics::Node node;
  node.set_node_id(0u);
  node.mutable_attributes()->set_is_keyboard_key(true);
  CreateOrUpdateSemanticNode(std::move(node));

  EXPECT_TRUE(screen_reader_context_->IsVirtualKeyboardFocused());
}

TEST_F(ScreenReaderContextTest, IsTextFieldFocused) {
  a11y_focus_manager_ptr_->SetA11yFocus(view_ref_helper_.koid(), 0u, [](auto...) {});
  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    node.set_role(fuchsia::accessibility::semantics::Role::TEXT_FIELD);
    CreateOrUpdateSemanticNode(std::move(node));
  }
  EXPECT_TRUE(screen_reader_context_->IsTextFieldFocused());

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    node.set_role(fuchsia::accessibility::semantics::Role::SEARCH_BOX);
    CreateOrUpdateSemanticNode(std::move(node));
  }

  EXPECT_TRUE(screen_reader_context_->IsTextFieldFocused());

  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    node.set_role(fuchsia::accessibility::semantics::Role::BUTTON);
    CreateOrUpdateSemanticNode(std::move(node));
  }

  EXPECT_FALSE(screen_reader_context_->IsTextFieldFocused());
}

TEST_F(ScreenReaderContextTest, FallbackToEnglishWhenLocaleIsUnknown) {
  auto a11y_focus_manager = std::make_unique<MockA11yFocusManager>();

  screen_reader_context_ = std::make_unique<a11y::ScreenReaderContext>(
      std::move(a11y_focus_manager), &tts_manager_, &mock_view_source_, "sr-RS");

  // Because the provided locale does not exist, check that it used en-US as a fallback.
  icu::Locale locale("en-US");
  EXPECT_EQ(screen_reader_context_->speaker()
                ->message_generator_for_test()
                ->message_formatter_for_test()
                ->locale(),
            locale);
}

TEST_F(ScreenReaderContextTest, UpdateCacheIfDescribableA11yFocusedNodeContentChanged) {
  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    CreateOrUpdateSemanticNode(std::move(node));
  }

  a11y_focus_manager_ptr_->SetA11yFocus(view_ref_helper_.koid(), 0u, [](auto...) {});
  EXPECT_FALSE(screen_reader_context_->UpdateCacheIfDescribableA11yFocusedNodeContentChanged());

  fuchsia::accessibility::semantics::Node clone;
  {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(0u);
    node.mutable_attributes()->set_label("foo");
    node.Clone(&clone);
    CreateOrUpdateSemanticNode(std::move(node));
  }

  EXPECT_TRUE(screen_reader_context_->UpdateCacheIfDescribableA11yFocusedNodeContentChanged());

  {
    fuchsia::accessibility::semantics::Node node;
    clone.Clone(&node);
    node.mutable_states()->set_selected(true);
    CreateOrUpdateSemanticNode(std::move(node));
  }

  EXPECT_TRUE(screen_reader_context_->UpdateCacheIfDescribableA11yFocusedNodeContentChanged());

  {
    fuchsia::accessibility::semantics::Node node;
    clone.Clone(&node);
    node.mutable_states()->set_selected(false);
    node.Clone(&clone);  // To update the value in clone.
    CreateOrUpdateSemanticNode(std::move(node));
  }

  EXPECT_TRUE(screen_reader_context_->UpdateCacheIfDescribableA11yFocusedNodeContentChanged());

  {
    fuchsia::accessibility::semantics::Node node;
    clone.Clone(&node);
    node.set_role(fuchsia::accessibility::semantics::Role::BUTTON);
    CreateOrUpdateSemanticNode(std::move(node));
  }

  // No change this time, as only the role changed and we care only about changes in attributes or
  // states.
  EXPECT_FALSE(screen_reader_context_->UpdateCacheIfDescribableA11yFocusedNodeContentChanged());
}

}  // namespace
}  // namespace accessibility_test
