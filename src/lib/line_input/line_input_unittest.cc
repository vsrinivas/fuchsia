// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/line_input/line_input.h"

#include <optional>

#include "gtest/gtest.h"

namespace line_input {

namespace {

// Some common terminal codes.
#define TERM_UP "\x1b[A"
#define TERM_DOWN "\x1b[B"
#define TERM_LEFT "\x1b[D"
#define TERM_RIGHT "\x1b[C"

// Dummy completion functions that return two completions.
std::vector<std::string> AutocompleteCallback(const std::string& line) {
  std::vector<std::string> result;
  result.push_back("one");
  result.push_back("two");
  return result;
}

}  // namespace

class TestLineInput : public LineInputEditor {
 public:
  TestLineInput(const std::string& prompt)
      : LineInputEditor(
            [this](const std::string& s) {
              if (accept_goes_to_history_)
                AddToHistory(s);
              accept_ = s;
            },
            prompt) {}

  // The "accept" value is the result of the most recent callback issuance.
  const std::optional<std::string>& accept() const { return accept_; }
  void ClearAccept() { accept_ = std::nullopt; }

  void ClearOutput() { output_.clear(); }

  // See variable below.
  void set_accept_goes_to_history(bool a) { accept_goes_to_history_ = a; }

  std::string GetAndClearOutput() {
    std::string ret = output_;
    ClearOutput();
    return ret;
  }

  // This input takes a string instead of one character at a time, returning true if the
  // callback was issued for the *last* character.
  bool OnInputStr(const std::string& input) {
    for (char c : input) {
      ClearAccept();
      OnInput(c);
    }
    return !!accept();
  }

  void SetLine(const std::string& input) {
    cur_line() = input;
    set_pos(input.size());
  }

  void SetPos(size_t pos) { set_pos(pos); }

 protected:
  void Write(const std::string& data) { output_.append(data); }

 private:
  std::string output_;

  // When set, the accept callback will automatically add the new line to history.
  bool accept_goes_to_history_ = false;

  // The parameter from the most recent "accept" call, or none if not called.
  std::optional<std::string> accept_;
};

TEST(LineInput, CursorCommands) {
  std::string prompt("Prompt ");
  TestLineInput input(prompt);

  // Basic prompt. "7C" at the end means cursor is @ 7th character.
  input.Show();
  EXPECT_EQ("\rPrompt \x1b[0K\r\x1B[7C", input.GetAndClearOutput());

  // Basic input with enter.
  input.OnInput('a');
  input.OnInput('b');
  EXPECT_FALSE(input.accept());
  input.OnInput('\r');
  ASSERT_TRUE(input.accept());
  EXPECT_EQ("ab", *input.accept());

  EXPECT_FALSE(input.OnInputStr("abcd"));
  EXPECT_EQ(4u, input.pos());

  // Basic cursor movement.
  input.OnInput(2);  // Control-B = left.
  EXPECT_EQ(3u, input.pos());
  input.OnInput(6);  // Control-F = right.
  EXPECT_EQ(4u, input.pos());
  input.OnInput(1);  // Control-A = home.
  EXPECT_EQ(0u, input.pos());
  input.OnInput(5);  // Control-E = end.
  EXPECT_EQ(4u, input.pos());

  // Longer escaped sequences.
  input.OnInputStr("\x1b[D");  // Left.
  EXPECT_EQ(3u, input.pos());
  input.OnInputStr("\x1b[C");  // Right.
  EXPECT_EQ(4u, input.pos());
  input.OnInputStr("\x1b[H");  // Home.
  EXPECT_EQ(0u, input.pos());
  input.OnInputStr("\x1b[F");  // End.
  EXPECT_EQ(4u, input.pos());
  input.OnInputStr("\x1b[1~");  // Home. Alternate.
  EXPECT_EQ(0u, input.pos());
  input.OnInputStr("\x1b[4~");  // End. Alternate.
  EXPECT_EQ(4u, input.pos());

  // Backspace.
  input.OnInput(127);  // Backspace.
  EXPECT_EQ(3u, input.pos());
  EXPECT_EQ("abc", input.GetLine());

  // Delete. This one also tests the line refresh commands.
  input.OnInput(1);  // Home.
  input.ClearOutput();
  input.OnInputStr("\x1b[3~");
  EXPECT_EQ("bc", input.GetLine());
  // "7C" at the end means cursor is at the 7th character (the "b").
  EXPECT_EQ("\rPrompt bc\x1b[0K\r\x1B[7C", input.GetAndClearOutput());
  EXPECT_EQ(0u, input.pos());
}

TEST(LineInput, CtrlD) {
  std::string prompt("Prompt ");
  TestLineInput input(prompt);
  input.Show();

  EXPECT_FALSE(input.OnInputStr("abcd"));
  // "abcd|"
  EXPECT_EQ(4u, input.pos());

  EXPECT_FALSE(input.OnInputStr("\x1b[D\x1b[D"));  // 2 x Left.
  // "ab|cd"
  EXPECT_EQ(2u, input.pos());

  input.OnInput(4);  // Ctrl+D
  // "ab|d"
  EXPECT_EQ("abd", input.GetLine());
  EXPECT_EQ(2u, input.pos());

  input.OnInputStr("\x1b[C");  // Right.
  // "abd|"
  EXPECT_EQ(3u, input.pos());
  EXPECT_EQ("abd", input.GetLine());

  input.OnInput(4);  // Ctrl+D
  // No change when hit Ctrl+D at the end of the line.
  EXPECT_EQ("abd", input.GetLine());
  EXPECT_EQ(3u, input.pos());

  // Erase everything and then exit.

  EXPECT_FALSE(input.OnInputStr("\x1b[D\x1b[D\x1b[D"));  // 3 x Left.
  // "|abd"
  EXPECT_EQ(0u, input.pos());

  input.OnInput(4);  // Ctrl+D
  // "|bd"
  EXPECT_EQ("bd", input.GetLine());
  EXPECT_EQ(0u, input.pos());

  input.OnInput(4);  // Ctrl+D
  // "|d"
  EXPECT_EQ("d", input.GetLine());
  EXPECT_EQ(0u, input.pos());

  input.OnInput(4);  // Ctrl+D
  // "|"
  EXPECT_EQ("", input.GetLine());
  EXPECT_EQ(0u, input.pos());

  // Ctrl+D on an empty line is exit.

  bool got_eof = false;
  input.SetEofCallback([&got_eof]() { got_eof = true; });
  input.OnInput(4);  // Ctrl+D
  EXPECT_TRUE(got_eof);
}

TEST(LineInput, History) {
  TestLineInput input("");
  input.set_accept_goes_to_history(true);
  input.Show();

  // Make some history.
  input.OnInputStr("one\r");
  input.OnInputStr("two\r");

  // Go up twice.
  EXPECT_FALSE(input.OnInputStr(TERM_UP TERM_UP));

  // Should have selected the first line and the cursor should be at the end.
  EXPECT_EQ("one", input.GetLine());
  EXPECT_EQ(3u, input.pos());

  // Append a letter and accept it.
  input.OnInputStr("s\r");

  // Start editing a new line with some input.
  input.OnInputStr("three");

  // Check history. Should be:
  //  ones
  //  two
  //  ones
  //  three
  EXPECT_EQ("three", input.GetLine());
  EXPECT_FALSE(input.OnInputStr(TERM_UP));
  EXPECT_EQ("ones", input.GetLine());
  EXPECT_FALSE(input.OnInputStr(TERM_UP));
  EXPECT_EQ("two", input.GetLine());
  EXPECT_FALSE(input.OnInputStr(TERM_UP));
  EXPECT_FALSE(input.OnInputStr(TERM_UP));  // From here, these are extra to
  EXPECT_FALSE(input.OnInputStr(TERM_UP));  // test that going beyond the top
  EXPECT_FALSE(input.OnInputStr(TERM_UP));  // stays stopped.
  EXPECT_EQ("ones", input.GetLine());

  // Going back to the bottom (also doing one extra one to test the boundary).
  EXPECT_FALSE(input.OnInputStr(TERM_DOWN TERM_DOWN TERM_DOWN TERM_DOWN));

  // Should have gotten the original non-accepted input back.
  EXPECT_EQ("three", input.GetLine());
}

TEST(LineInput, HistoryEdgeCases) {
  TestLineInput input("");

  input.AddToHistory("one");
  ASSERT_EQ(input.GetHistory().size(), 2u);

  // If input is empty, it should not add to history.
  input.AddToHistory("");
  ASSERT_EQ(input.GetHistory().size(), 2u);

  // Same input should not work.
  input.AddToHistory("one");
  ASSERT_EQ(input.GetHistory().size(), 2u);

  // A past input should work.
  input.AddToHistory("two");
  ASSERT_EQ(input.GetHistory().size(), 3u);
  input.AddToHistory("one");
  ASSERT_EQ(input.GetHistory().size(), 4u);
}

TEST(LineInput, Completions) {
  TestLineInput input("");
  input.SetAutocompleteCallback(&AutocompleteCallback);

  input.Show();
  input.OnInput('z');

  // Send one tab, should get the first suggestion.
  input.OnInput(9);
  EXPECT_EQ("one", input.GetLine());
  EXPECT_EQ(3u, input.pos());

  // Second suggestion.
  input.OnInput(9);
  EXPECT_EQ("two", input.GetLine());
  EXPECT_EQ(3u, input.pos());

  // Again should go back to original text.
  input.OnInput(9);
  EXPECT_EQ("z", input.GetLine());
  EXPECT_EQ(1u, input.pos());

  // Should wrap around to the first suggestion.
  input.OnInput(9);
  EXPECT_EQ("one", input.GetLine());
  EXPECT_EQ(3u, input.pos());

  // Typing should append.
  input.OnInput('s');
  EXPECT_EQ("ones", input.GetLine());
  EXPECT_EQ(4u, input.pos());

  // Tab again should give the same suggestions.
  input.OnInput(9);
  EXPECT_EQ("one", input.GetLine());
  EXPECT_EQ(3u, input.pos());

  // Send an escape sequence "left" which should accept the suggestion and
  // execute the sequence.
  input.OnInputStr("\x1b[D");
  EXPECT_EQ("one", input.GetLine());
  EXPECT_EQ(2u, input.pos());
}

TEST(LineInput, Scroll) {
  TestLineInput input("ABCDE");
  input.SetMaxCols(10);

  input.Show();
  input.ClearOutput();

  // Write up to the 9th character, which should be the last character printed
  // until scrolling starts. It should have used the optimized "just write the
  // characters" code path for everything after the prompt.
  EXPECT_FALSE(input.OnInputStr("FGHI"));
  EXPECT_EQ("FGHI", input.GetAndClearOutput());

  // Add a 10th character. The whole line should scroll one to the left,
  // leaving the cursor at the last column (column offset 9 = "9C" at the end).
  input.OnInput('J');
  EXPECT_EQ("\rBCDEFGHIJ\x1b[0K\r\x1B[9C", input.GetAndClearOutput());

  // Move left, the line should scroll back.
  input.OnInput(2);  // 2 = Control-B.
  EXPECT_EQ("\rABCDEFGHIJ\x1b[0K\r\x1B[9C", input.GetAndClearOutput());
}

TEST(LineInput, NegAck) {
  TestLineInput input("ABCDE");
  input.Show();

  // Empty should remain with them prompt.
  input.OnInput(SpecialCharacters::kKeyControlU);
  EXPECT_EQ(input.GetLine(), "");

  // Adding characters and then Control-U should clear.
  input.OnInputStr("12345");
  input.OnInput(SpecialCharacters::kKeyControlU);
  EXPECT_EQ(input.GetLine(), "");

  // In the middle of the line should clear until the cursor.
  input.OnInputStr("0123456789");
  EXPECT_FALSE(input.OnInputStr(TERM_LEFT));
  EXPECT_FALSE(input.OnInputStr(TERM_LEFT));
  EXPECT_FALSE(input.OnInputStr(TERM_LEFT));
  EXPECT_FALSE(input.OnInputStr(TERM_LEFT));
  input.OnInput(SpecialCharacters::kKeyControlU);
  EXPECT_EQ(input.GetLine(), "6789");
  EXPECT_EQ(input.pos(), 0u);
}

TEST(LineInput, EndOfTransimission) {
  TestLineInput input("[prompt] ");
  input.Show();

  //             v
  input.SetLine("First Second Third");
  input.SetPos(0);
  input.OnInput(SpecialCharacters::kKeyControlW);
  EXPECT_EQ(input.GetLine(), "First Second Third");

  //               v
  input.SetLine("First Second Third");
  input.SetPos(2);
  input.OnInput(SpecialCharacters::kKeyControlW);
  EXPECT_EQ(input.GetLine(), "rst Second Third");

  //                  v
  input.SetLine("First Second Third");
  input.SetPos(5);
  input.OnInput(SpecialCharacters::kKeyControlW);
  EXPECT_EQ(input.GetLine(), " Second Third");

  //                     v
  input.SetLine("First Second Third");
  input.SetPos(8);
  input.OnInput(SpecialCharacters::kKeyControlW);
  EXPECT_EQ(input.GetLine(), "First cond Third");

  //                         v
  input.SetLine("First Second Third");
  input.SetPos(12);
  input.OnInput(SpecialCharacters::kKeyControlW);
  EXPECT_EQ(input.GetLine(), "First  Third");

  //                            v
  input.SetLine("First Second Third");
  input.SetPos(15);
  input.OnInput(SpecialCharacters::kKeyControlW);
  EXPECT_EQ(input.GetLine(), "First Second ird");

  //                               v
  input.SetLine("First Second Third");
  input.OnInput(SpecialCharacters::kKeyControlW);
  EXPECT_EQ(input.GetLine(), "First Second ");
}

TEST(LineInput, Transpose) {
  TestLineInput input("[prompt] ");
  input.Show();

  //             v
  input.SetLine("First Second Third");
  input.SetPos(0);
  input.OnInput(SpecialCharacters::kKeyControlT);
  EXPECT_EQ(input.GetLine(), "First Second Third");

  //              v
  input.SetLine("First Second Third");
  input.SetPos(1);
  input.OnInput(SpecialCharacters::kKeyControlT);
  EXPECT_EQ(input.GetLine(), "First Second Third");

  //               v
  input.SetLine("First Second Third");
  input.SetPos(2);
  input.OnInput(SpecialCharacters::kKeyControlT);
  EXPECT_EQ(input.GetLine(), "iFrst Second Third");

  //                               v
  input.SetLine("First Second Third");
  input.SetPos(18);
  input.OnInput(SpecialCharacters::kKeyControlT);
  EXPECT_EQ(input.GetLine(), "First Second Thidr");
}

TEST(LineInput, DeleteEnd) {
  TestLineInput input("[prompt] ");
  input.Show();

  //             v
  input.SetLine("First Second Third");
  input.SetPos(0);
  input.OnInput(SpecialCharacters::kKeyControlK);
  EXPECT_EQ(input.GetLine(), "");

  //               v
  input.SetLine("First Second Third");
  input.SetPos(2);
  input.OnInput(SpecialCharacters::kKeyControlK);
  EXPECT_EQ(input.GetLine(), "Fi");

  //                  v
  input.SetLine("First Second Third");
  input.SetPos(5);
  input.OnInput(SpecialCharacters::kKeyControlK);
  EXPECT_EQ(input.GetLine(), "First");

  //                     v
  input.SetLine("First Second Third");
  input.SetPos(8);
  input.OnInput(SpecialCharacters::kKeyControlK);
  EXPECT_EQ(input.GetLine(), "First Se");

  //                         v
  input.SetLine("First Second Third");
  input.SetPos(12);
  input.OnInput(SpecialCharacters::kKeyControlK);
  EXPECT_EQ(input.GetLine(), "First Second");

  //                               v
  input.SetLine("First Second Third");
  input.OnInput(SpecialCharacters::kKeyControlK);
  EXPECT_EQ(input.GetLine(), "First Second Third");
}

TEST(LineInput, CancelCommand) {
  TestLineInput input("[prompt] ");
  input.Show();

  //             v
  input.SetLine("First Second Third");
  input.SetPos(0);
  input.OnInput(SpecialCharacters::kKeyControlC);
  EXPECT_EQ(input.GetLine(), "");

  //               v
  input.SetLine("First Second Third");
  input.SetPos(2);
  input.OnInput(SpecialCharacters::kKeyControlC);
  EXPECT_EQ(input.GetLine(), "");

  //                               v
  input.SetLine("First Second Third");
  input.SetPos(18);
  input.OnInput(SpecialCharacters::kKeyControlC);
  EXPECT_EQ(input.GetLine(), "");
}

TEST(LineInput, ReverseHistory_Select) {
  TestLineInput input("> ");

  // Add some history.
  input.AddToHistory("prefix postfix1");  // Index 5.
  input.AddToHistory("prefix postfix2");  // Index 4.
  input.AddToHistory("prefix postfix3");  // Index 3.
  input.AddToHistory("other prefix");     // Index 2.
  input.AddToHistory("different");        // Index 1.

  input.Show();
  input.OnInput(SpecialCharacters::kKeyControlR);
  ASSERT_TRUE(input.in_reverse_history_mode());

  EXPECT_FALSE(input.OnInputStr("post"));
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`post': ");
  EXPECT_EQ(input.reverse_history_index(), 3u);
  // Pos:                                        |       v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "prefix postfix3");
  EXPECT_EQ(input.pos(), 7u);

  // Selecting should get this suggestion out.
  input.OnInput(SpecialCharacters::kKeyEnter);
  ASSERT_FALSE(input.in_reverse_history_mode());
  // Pos:                 |               v
  EXPECT_EQ(input.GetLine(), "prefix postfix3");
  EXPECT_EQ(input.pos(), 15u);
}

TEST(LineInput, ReverseHistory_SpecificSearch) {
  TestLineInput input("> ");

  // Add some history.
  input.AddToHistory("prefix postfix1");  // Index 5.
  input.AddToHistory("prefix postfix2");  // Index 4.
  input.AddToHistory("prefix postfix3");  // Index 3.
  input.AddToHistory("other prefix");     // Index 2.
  input.AddToHistory("different");        // Index 1.

  input.Show();

  input.OnInput(SpecialCharacters::kKeyControlR);
  ASSERT_TRUE(input.in_reverse_history_mode());

  input.OnInput('f');
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`f': ");
  EXPECT_EQ(input.reverse_history_index(), 1u);
  // Pos:                                        |  v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "different");
  EXPECT_EQ(input.pos(), 2u);

  input.OnInput('i');
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`fi': ");
  EXPECT_EQ(input.reverse_history_index(), 2u);
  // Pos:                                        |         v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "other prefix");
  EXPECT_EQ(input.pos(), 9u);

  input.OnInput('x');
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`fix': ");
  EXPECT_EQ(input.reverse_history_index(), 2u);
  // Pos:                                        |         v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "other prefix");
  EXPECT_EQ(input.pos(), 9u);

  input.OnInput('3');
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`fix3': ");
  EXPECT_EQ(input.reverse_history_index(), 3u);
  // Pos:                                        |           v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "prefix postfix3");
  EXPECT_EQ(input.pos(), 11u);

  input.OnInput('3');
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`fix33': ");
  EXPECT_EQ(input.reverse_history_index(), 0u);
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "");
  EXPECT_EQ(input.pos(), 0u);

  // Deleting should return to the suggestion.
  input.OnInput(SpecialCharacters::kKeyBackspace);
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`fix3': ");
  EXPECT_EQ(input.reverse_history_index(), 3u);
  // Pos:                                        |           v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "prefix postfix3");
  EXPECT_EQ(input.pos(), 11u);
}

TEST(LineInput, ReverseHistory_RepeatedSearch) {
  TestLineInput input("> ");

  // Add some history.
  input.AddToHistory("prefix postfix1");  // Index 5.
  input.AddToHistory("prefix postfix2");  // Index 4.
  input.AddToHistory("prefix postfix3");  // Index 3.
  input.AddToHistory("other prefix");     // Index 2.
  input.AddToHistory("different");        // Index 1.

  input.Show();
  ASSERT_FALSE(input.in_reverse_history_mode());

  input.OnInput(SpecialCharacters::kKeyControlR);

  // We should be in reverse history mode, but no suggestion should be made.
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`': ");
  EXPECT_EQ(input.reverse_history_index(), 0u);
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "");
  EXPECT_EQ(input.pos(), 0u);

  // Start writing should match.
  input.OnInput('f');
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`f': ");
  EXPECT_EQ(input.reverse_history_index(), 1u);
  // Pos:                                        |  v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "different");
  EXPECT_EQ(input.pos(), 2u);

  // Ctrl-R should move to the next suggestion.
  input.OnInput(SpecialCharacters::kKeyControlR);
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`f': ");
  EXPECT_EQ(input.reverse_history_index(), 2u);
  // Pos:                                        |         v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "other prefix");
  EXPECT_EQ(input.pos(), 9u);

  input.OnInput(SpecialCharacters::kKeyControlR);
  input.OnInput(SpecialCharacters::kKeyControlR);
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`f': ");
  EXPECT_EQ(input.reverse_history_index(), 4u);
  // Pos:                                        |   v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "prefix postfix2");
  EXPECT_EQ(input.pos(), 3u);

  // More Ctrl-R should roll-over.
  input.OnInput(SpecialCharacters::kKeyControlR);
  input.OnInput(SpecialCharacters::kKeyControlR);
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`f': ");
  EXPECT_EQ(input.reverse_history_index(), 0u);
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "");
  EXPECT_EQ(input.pos(), 0u);

  // One more should start again.
  input.OnInput(SpecialCharacters::kKeyControlR);
  input.OnInput(SpecialCharacters::kKeyControlR);
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`f': ");
  EXPECT_EQ(input.reverse_history_index(), 2u);
  // Pos:                                        |         v
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "other prefix");
  EXPECT_EQ(input.pos(), 9u);

  // Deleting should show no suggestion.
  input.OnInput(SpecialCharacters::kKeyBackspace);
  ASSERT_TRUE(input.in_reverse_history_mode());
  EXPECT_EQ(input.GetReverseHistoryPrompt(), "(reverse-i-search)`': ");
  EXPECT_EQ(input.reverse_history_index(), 0u);
  EXPECT_EQ(input.GetReverseHistorySuggestion(), "");
  EXPECT_EQ(input.pos(), 0u);
}

}  // namespace line_input
