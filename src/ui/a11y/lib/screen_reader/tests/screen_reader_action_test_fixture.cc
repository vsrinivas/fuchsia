// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/tests/screen_reader_action_test_fixture.h"

namespace accessibility_test {

void ScreenReaderActionTest::SetUp() {
  mock_semantic_provider_ = std::make_unique<MockSemanticProvider>(nullptr, nullptr);

  mock_semantics_source_ = std::make_unique<MockSemanticsSource>();
  mock_injector_manager_ = std::make_unique<MockInjectorManager>();

  action_context_.semantics_source = mock_semantics_source_.get();
  action_context_.injector_manager = mock_injector_manager_.get();

  mock_screen_reader_context_ = std::make_unique<MockScreenReaderContext>();

  mock_a11y_focus_manager_ptr_ = mock_screen_reader_context_->mock_a11y_focus_manager_ptr();
  mock_speaker_ptr_ = mock_screen_reader_context_->mock_speaker_ptr();
}

}  // namespace accessibility_test
