// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/console_impl.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"

namespace zxdb {

namespace {

class MockLineInput : public line_input::LineInputEditor {
 public:
  MockLineInput(AcceptCallback accept_cb, const std::string& prompt)
      : LineInputEditor(std::move(accept_cb), prompt) {}

  void Write(const std::string& data) override {
    // Just discard any output. This could be saved if future tests need it.
  }
};

class ConsoleImplTest : public TestWithLoop {
 public:
  ConsoleImplTest() {
    session = std::make_unique<Session>();
    console = std::make_unique<ConsoleImpl>(
        session.get(),
        [](line_input::LineInput::AcceptCallback accept_cb,
           const std::string& prompt) -> std::unique_ptr<line_input::LineInput> {
          return std::make_unique<MockLineInput>(std::move(accept_cb), prompt);
        });
    console->Init();
  }

  std::unique_ptr<Session> session;
  std::unique_ptr<ConsoleImpl> console;
};

}  // namespace

TEST_F(ConsoleImplTest, ControlC) {
  // Sending an input + Control-C should clear the line.
  console->line_input_.OnInput('a');
  EXPECT_EQ("a", console->line_input_.GetLine());
  console->line_input_.OnInput(line_input::SpecialCharacters::kKeyControlC);
  EXPECT_EQ("", console->line_input_.GetLine());

  // Sending control-C with no line should issue a "pause".
  console->line_input_.OnInput(line_input::SpecialCharacters::kKeyControlC);
  // There should still be no input.
  EXPECT_EQ("", console->line_input_.GetLine());

  // One command back should be the pause command inserted by the Control-C. Note that since this
  // is testing the real ConsoleImpl, it may load the current user's history so the history may be
  // longer than the one we make here.
  auto history = console->line_input_.GetHistory();
  ASSERT_LE(1u, history.size());
  EXPECT_EQ("pause --clear-state", history[1]);
}

}  // namespace zxdb
