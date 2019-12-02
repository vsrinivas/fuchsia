// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/line_input/modal_line_input.h"

#include <optional>

#include "gtest/gtest.h"
#include "src/lib/line_input/test_line_input.h"

namespace line_input {

namespace {

class TestModalLineInput : public ModalLineInput {
 public:
  TestModalLineInput() : ModalLineInput() {}
  ~TestModalLineInput() override {}

 protected:
  std::unique_ptr<LineInput> MakeLineInput(AcceptCallback accept_cb,
                                           const std::string& prompt) override {
    return std::make_unique<TestLineInput>(prompt, std::move(accept_cb));
  }
};

}  // namespace

// Runs two asynchronous modal prompts and makes sure they each run in sequence.
TEST(ModalLineInputTest, Nested) {
  std::optional<std::string> accept_line;

  TestModalLineInput input;
  input.Init([&accept_line](const std::string& line) { accept_line = line; }, "Prompt ");
  input.Show();

  // Send some regular input.
  input.OnInput('a');
  input.OnInput('b');

  // Start a modal prompt. The input "x" keeps it open, "m1" closes it.
  bool got_prompt_1 = false;
  input.BeginModal("Modal1 ", [&input, &got_prompt_1](const std::string& line) {
    got_prompt_1 = true;
    EXPECT_TRUE(line == "x" || line == "m1");
    if (line == "m1")
      input.EndModal();
  });

  // Start a second modal prompt before the first one is accepted.
  bool got_prompt_2 = false;
  input.BeginModal("Modal1 ", [&input, &got_prompt_2](const std::string& line) {
    got_prompt_2 = true;
    EXPECT_EQ("m2", line);
    input.EndModal();
  });

  // Input should now go to the modal promot #1.
  input.OnInput('x');
  input.OnInput('\r');
  EXPECT_TRUE(got_prompt_1);

  // That input should keep it open and read another line.
  got_prompt_1 = false;
  input.OnInput('m');
  input.OnInput('1');
  input.OnInput('\r');
  EXPECT_TRUE(got_prompt_1);

  // It should now switch to the second modal prompt.
  input.OnInput('m');
  input.OnInput('2');
  input.OnInput('\r');
  EXPECT_TRUE(got_prompt_2);

  // Further input should go to the regular prompt.
  input.OnInput('c');
  input.OnInput('\r');

  // The original + new input should be there.
  EXPECT_EQ("abc", accept_line);
}

}  // namespace line_input
