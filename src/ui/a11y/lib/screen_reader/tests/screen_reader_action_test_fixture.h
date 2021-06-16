// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_SCREEN_READER_ACTION_TEST_FIXTURE_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_SCREEN_READER_ACTION_TEST_FIXTURE_H_

#include <lib/gtest/test_loop_fixture.h>
#include <zircon/types.h>

#include <memory>

#include <gmock/gmock.h>

#include "src/ui/a11y/lib/input_injection/tests/mocks/mock_injector_manager.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_source.h"

namespace accessibility_test {

// Test fixture that sets up mocks required to test screen reader action
// classes.
class ScreenReaderActionTest : public gtest::TestLoopFixture {
 public:
  ScreenReaderActionTest() = default;
  virtual ~ScreenReaderActionTest() override = default;

  // Overrides should call this method before performing additional setup.
  virtual void SetUp() override;

 protected:
  MockSemanticsSource* mock_semantics_source() { return mock_semantics_source_.get(); }
  a11y::ScreenReaderAction::ActionContext* action_context() { return &action_context_; }
  MockScreenReaderContext* mock_screen_reader_context() {
    return mock_screen_reader_context_.get();
  }
  MockSemanticProvider* mock_semantic_provider() { return mock_semantic_provider_.get(); }
  MockA11yFocusManager* mock_a11y_focus_manager() { return mock_a11y_focus_manager_ptr_; }
  MockScreenReaderContext::MockSpeaker* mock_speaker() { return mock_speaker_ptr_; }
  MockInjectorManager* mock_injector_manager() { return mock_injector_manager_.get(); }

 private:
  std::unique_ptr<MockSemanticsSource> mock_semantics_source_;
  a11y::ScreenReaderAction::ActionContext action_context_;
  std::unique_ptr<MockScreenReaderContext> mock_screen_reader_context_;
  std::unique_ptr<MockSemanticProvider> mock_semantic_provider_;
  MockA11yFocusManager* mock_a11y_focus_manager_ptr_;
  MockScreenReaderContext::MockSpeaker* mock_speaker_ptr_;
  std::unique_ptr<MockInjectorManager> mock_injector_manager_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_TESTS_SCREEN_READER_ACTION_TEST_FIXTURE_H_
