// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_focus_highlight_manager.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"

namespace accessibility_test {
namespace {

class ScreenReaderContextTest : public gtest::RealLoopFixture {
 public:
  ScreenReaderContextTest() : context_provider_(), tts_manager_(context_provider_.context()) {
    // Initialize A11yFocusManager.
    auto a11y_focus_manager = std::make_unique<a11y::A11yFocusManager>(
        &mock_focus_requester_, &mock_focus_registry_, &mock_focus_highlight_manager_);

    // Store raw pointer to A11yFocusManager.
    a11y_focus_manager_ptr_ = a11y_focus_manager.get();

    // Initialize screen reader context.
    screen_reader_context_ =
        std::make_unique<a11y::ScreenReaderContext>(std::move(a11y_focus_manager), &tts_manager_);
  }

  sys::testing::ComponentContextProvider context_provider_;
  MockAccessibilityFocusChainRequester mock_focus_requester_;
  MockAccessibilityFocusChainRegistry mock_focus_registry_;
  MockFocusHighlightManager mock_focus_highlight_manager_;
  a11y::A11yFocusManager* a11y_focus_manager_ptr_ = nullptr;
  a11y::TtsManager tts_manager_;
  std::unique_ptr<a11y::ScreenReaderContext> screen_reader_context_;
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
            a11y::ScreenReaderContext::SemanticLevel::kNormalNavigation);
  screen_reader_context_->set_semantic_level(a11y::ScreenReaderContext::SemanticLevel::kWord);
  EXPECT_EQ(screen_reader_context_->semantic_level(),
            a11y::ScreenReaderContext::SemanticLevel::kWord);
}

}  // namespace
}  // namespace accessibility_test
