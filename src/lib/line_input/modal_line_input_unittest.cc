// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/line_input/modal_line_input.h"

#include <optional>

#include <gtest/gtest.h>

#include "src/lib/line_input/test_line_input.h"

namespace line_input {

namespace {

// Factory function for the ModalLineInput's underlying input object.
std::unique_ptr<LineInput> MakeTestLineInput(LineInput::AcceptCallback accept_cb,
                                             const std::string& prompt) {
  return std::make_unique<TestLineInput>(prompt, std::move(accept_cb));
}

}  // namespace

// Runs two asynchronous modal prompts and makes sure they each run in sequence.
TEST(ModalLineInputTest, Nested) {
  std::optional<std::string> accept_line;

  ModalLineInput input(&MakeTestLineInput);
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

TEST(ModalLineInputTest, ModalGetOption) {
  ModalLineInput input(&MakeTestLineInput);
  std::optional<std::string> read_line;  // Last non-modal result.
  input.Init([&read_line](const std::string& line) { read_line = line; }, "Prompt ");
  input.Show();

  ModalPromptOptions options;
  options.require_enter = true;
  options.case_sensitive = true;
  options.options.push_back("y");
  options.options.push_back("n");

  std::string result;
  input.ModalGetOption(options, ">", [&result](const std::string& line) { result = line; });

  // Empty input should get rejected.
  input.OnInput('\r');
  EXPECT_TRUE(result.empty());

  // Invalid input should get rejected.
  input.OnInput('X');
  input.OnInput('\r');
  EXPECT_TRUE(result.empty());

  // It was marked case-sensitive so uppercase should be rejected.
  input.OnInput('Y');
  input.OnInput('\r');
  EXPECT_TRUE(result.empty());

  // It was marked case-sensitive so uppercase should be rejected.
  input.OnInput('y');
  EXPECT_TRUE(result.empty());  // Because enter was marked required.
  input.OnInput('\r');
  ASSERT_EQ("y", result);

  // Should have gone back to normal mode.
  input.OnInput('z');
  input.OnInput('\r');
  ASSERT_TRUE(read_line);
  EXPECT_EQ("z", *read_line);

  // Now try one with the opposite options.
  options.require_enter = false;
  options.case_sensitive = false;
  result.clear();
  input.ModalGetOption(options, ">", [&result](const std::string& line) { result = line; });

  // Invalid input should still be rejected.
  input.OnInput('X');
  input.OnInput('\r');
  EXPECT_TRUE(result.empty());

  // Case-insensitive uppercase should implicitly accept with new newline required.
  input.OnInput('Y');
  ASSERT_EQ("y", result);  // Result should be lower-cased.

  // Should have gone back to normal mode.
  read_line = std::nullopt;
  input.OnInput('y');
  input.OnInput('\r');
  ASSERT_TRUE(read_line);
  EXPECT_EQ("y", *read_line);

  // Control-C will normally do nothing.
  result.clear();
  input.ModalGetOption(options, ">", [&result](const std::string& line) { result = line; });
  input.OnInput('a');
  input.OnInput(SpecialCharacters::kKeyControlC);
  EXPECT_TRUE(result.empty());
  input.OnInput(SpecialCharacters::kKeyBackspace);
  input.OnInput('y');
  EXPECT_EQ("y", result);

  // Setting a cancel response will make it return that.
  result.clear();
  options.cancel_option = "n";
  input.ModalGetOption(options, ">", [&result](const std::string& line) { result = line; });
  input.OnInput(SpecialCharacters::kKeyControlC);
  EXPECT_EQ("n", result);
}

TEST(ModalLineInputTest, SetCurrentInput) {
  ModalLineInput input(&MakeTestLineInput);
  std::optional<std::string> read_line;  // Last non-modal result.
  input.Init([&read_line](const std::string& line) { read_line = line; }, "Prompt ");
  input.Show();

  input.OnInput('a');
  EXPECT_FALSE(read_line);  // Shouldn't have issued any callbacks.
  EXPECT_EQ("a", input.GetLine());

  // Replace the contents.
  input.SetCurrentInput("foo");
  EXPECT_FALSE(read_line);  // Shouldn't have issued any callbacks.
  EXPECT_EQ("foo", input.GetLine());

  // The cursor should be at the end of the line for additional input.
  input.OnInput('m');
  EXPECT_EQ("foom", input.GetLine());

  input.OnInput(SpecialCharacters::kKeyEnter);
  EXPECT_TRUE(read_line);
  EXPECT_EQ("foom", *read_line);

  // Add some history.
  input.AddToHistory("history");

  // Go up, the current line should be the history value.
  input.OnInput(SpecialCharacters::kKeyControlP);
  EXPECT_EQ("history", input.GetLine());

  // Set the input to empty and go up again. The history item should still be there.
  input.SetCurrentInput(std::string());
  input.OnInput(SpecialCharacters::kKeyControlP);
  EXPECT_EQ("history", input.GetLine());
}

}  // namespace line_input
