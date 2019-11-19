// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_LINE_INPUT_LINE_INPUT_H_
#define SRC_LIB_LINE_INPUT_LINE_INPUT_H_

#include <deque>
#include <string>
#include <vector>

#include "lib/fit/function.h"

#if !defined(__Fuchsia__)
struct termios;
#endif

namespace line_input {

struct SpecialCharacters {
  static constexpr char kKeyControlA = 1;
  static constexpr char kKeyControlB = 2;
  static constexpr char kKeyControlC = 3;
  static constexpr char kKeyControlD = 4;
  static constexpr char kKeyControlE = 5;
  static constexpr char kKeyControlF = 6;
  static constexpr char kKeyControlH = 8;
  static constexpr char kKeyTab = 9;
  static constexpr char kKeyNewline = 10;
  static constexpr char kKeyControlK = 11;
  static constexpr char kKeyFormFeed = 12;
  static constexpr char kKeyEnter = 13;
  static constexpr char kKeyControlN = 14;
  static constexpr char kKeyControlP = 16;
  static constexpr char kKeyControlR = 18;
  static constexpr char kKeyControlT = 20;
  static constexpr char kKeyControlU = 21;
  static constexpr char kKeyControlW = 23;
  static constexpr char kKeyEsc = 27;
  static constexpr char kKeyBackspace = 127;

  // Escape sequences for terminal output.
  static const char* kTermBeginningOfLine;
  static const char* kTermClearToEnd;
  static const char* kTermCursorToColFormat;  // printf format.
};

// Abtract base class for line input.
//
// This class implements a push model for input of characters, allowing it to be used in
// asynchronous contexts.
//
// The model is you create a LineInput class outside of the input loop. It encapsulates the history
// state and remembers the prompt. When you want to read a line:
//
//  1. Call BeginReadLine().
//  2. Push data to it via OnInput().
//  3. On an accept callback:
//     3a. Check IsEof() and get the input from GetLine().
//     3b. Add line to history if desired.
//  4. Repeat.
class LineInput {
 public:
  virtual ~LineInput() = default;

  // Called with the user input when the user acceps a line.
  using AcceptCallback = fit::function<void(const std::string&)>;

  // Given some typing, returns a prioritized list of completions.
  using AutocompleteCallback = fit::function<std::vector<std::string>(const std::string&)>;

  // Setup -----------------------------------------------------------------------------------------

  // Provides the callback for tab completion.
  virtual void SetAutocompleteCallback(AutocompleteCallback cb) = 0;

  // Sets the maximum width of a line. Beyond this the input will scroll. Setitng to 0 will disable
  // horizontal scrolling.
  virtual void SetMaxCols(size_t max) = 0;

  // Querying --------------------------------------------------------------------------------------

  // Returns true if EOF has been indicated (Control-D from the terminal). This will be valid for
  // the current line only. It will be reset for the next BeginReadLine().
  virtual bool IsEof() const = 0;

  // Returns the current input text.
  virtual const std::string& GetLine() const = 0;

  // Returns the current history. The most resent input is at the begin().
  virtual const std::deque<std::string>& GetHistory() const = 0;

  // State -----------------------------------------------------------------------------------------

  // Call to initialize reading a new line.
  virtual void BeginReadLine() = 0;

  // Provides one character of input to the editor. Callbacks for autocomplete or line done will be
  // issued from within this function.
  virtual void OnInput(char c) = 0;

  // Adds the given line to history. If the history is longer than max_history_, the oldest thing
  // will be deleted.
  //
  // Only valid to be called before BeginReadLine() starts the next line input.
  virtual void AddToHistory(const std::string& line) = 0;

  // The input can be hidden and re-shown. Hiding it will erase the current line and put the cursor
  // at the beginning of the line, but not change any internal state. Showing it again will repaint
  // the line at the new cursor position. This allows other output to be printed to the screen
  // without interfering with the input.
  //
  // OnInput() should not be called while hidden.
  virtual void Hide() = 0;
  virtual void Show() = 0;

  // Enables and disables raw mode if applicable.
  virtual void EnsureRawMode() = 0;
  virtual void EnsureNoRawMode() = 0;
};

// Implementation of LineInput that implements the editing state. Output is still abstract to
// allow for output to different places.
class LineInputEditor : public LineInput {
 public:
  explicit LineInputEditor(AcceptCallback accept_cb, const std::string& prompt);
  virtual ~LineInputEditor();

  // LineInput implementation.
  void SetAutocompleteCallback(AutocompleteCallback cb) override;
  void SetMaxCols(size_t max) override;
  bool IsEof() const override;
  const std::string& GetLine() const override;
  const std::deque<std::string>& GetHistory() const override;
  void BeginReadLine() override;
  void OnInput(char c) override;
  void AddToHistory(const std::string& line) override;
  void Hide() override;
  void Show() override;
  void EnsureRawMode() override {}
  void EnsureNoRawMode() override {}

  size_t pos() const { return pos_; }

  bool in_reverse_history_mode() const { return reverse_history_mode_; }
  size_t reverse_history_index() const { return reverse_history_index_; }

  // Exposed for testing purposes.
  std::string GetReverseHistoryPrompt() const;
  std::string GetReverseHistorySuggestion() const;

 protected:
  // Abstract output function, overridden by a derived class to output to
  // screen.
  virtual void Write(const std::string& data) = 0;

  // Helper to return the current line of text.
  std::string& cur_line() { return history_[history_index_]; }

  // Useful for testing.
  void set_pos(size_t pos) { pos_ = pos; }

  const std::string& prompt() const { return prompt_; }
  void set_prompt(std::string prompt) { prompt_ = std::move(prompt); }

 private:
  void HandleEscapedInput(char c);

  void HandleBackspace();
  void HandleDelete();
  // FormFeed is the name of Ctrl-L in ASCII world.
  void HandleFormFeed();
  void HandleEnter();
  void HandleTab();
  // NegAck is the name of Ctrl-U in ASCII world.
  void HandleNegAck();
  // EndOfTransimission is the name for Ctrl-W in ASCII world.
  void HandleEndOfTransimission();
  // EndOfFile means Ctrl-D with an empty input line.
  void HandleEndOfFile();

  // ReverseHistory means Ctrl-R.
  void HandleReverseHistory(char c);
  void StartReverseHistoryMode();
  void EndReverseHistoryMode(bool accept_suggestion);
  void SearchNextReverseHistory(bool restart);

  void Insert(char c);
  void MoveLeft();
  void MoveRight();
  void MoveUp();
  void MoveDown();
  void MoveHome();
  void MoveEnd();

  void TransposeLastTwoCharacters();
  void CancelCommand();
  void DeleteToEnd();

  void CancelCompletion();
  void AcceptCompletion();

  void RepaintLine();

  void ResetLineState();

  AcceptCallback accept_callback_;
  std::string prompt_;

  size_t max_cols_ = 0;
  AutocompleteCallback autocomplete_callback_;

  // Indicates whether the line is currently visible (as controlled by
  // Show()/Hide()).
  bool visible_ = true;

  // Indicates whether a line edit is in progress.
  bool editing_ = false;

  // Set to true once a Ctrl-D is read, indicating that the main loop should
  // exit.
  bool eof_ = false;

  // The history is basically the line stack going back in time as indices
  // increase. The currently viewed line is at [history_index_] and this is
  // where editing happens. When you start a new text entry, a new history
  // item is added and you delete it.
  //
  // This is simple but can be a bit confusing if you go back, edit, and then
  // press enter. The history item itself will be edited, and the same edited
  // version will be added again as the most recent history entry.
  //
  // This is weird because the editing has actually changed history. A more
  // complex model might be to maintain a virtual shadow copy of history that
  // you edit, and this shadow copy is replaced with the actual history
  // whenever you start editing a new line.
  std::deque<std::string> history_;  // front() is newest.
  size_t history_index_ = 0;         // Offset from history_.front().
  const size_t max_history_ = 256;

  bool completion_mode_ = false;
  std::vector<std::string> completions_;
  size_t completion_index_ = 0;

  // Tracks the current line's state before suggesting completions so we can
  // put them back if necessary. Only valid when completion_mode_ = true.
  std::string line_before_completion_;
  size_t pos_before_completion_;

  // When an escape is read, we enter "escaped input" mode which interprets the
  // next few characters input as an escape sequence.
  bool reading_escaped_input_ = false;
  std::string escape_sequence_;

  bool reverse_history_mode_ = false;
  std::string reverse_history_input_;

  // Index within history the reverse search suggestion current is. 0 means not found, as that is
  // pointing to the current line, which we don't want to do a history search in.
  size_t reverse_history_index_ = 0;

  size_t pos_;  // Current editing position.
};

// Implementation of LineInput that prints to stdout. The caller is still
// responsible for providing input asynchronously. The initial width of the
// output will be automatically derived from the terminal associated wit, as that is
// pointing to the current line, which we don't want to do a history search in.
// stdout (if any).
class LineInputStdout : public LineInputEditor {
 public:
  LineInputStdout(AcceptCallback accept_cb, const std::string& prompt);
  ~LineInputStdout() override;

  // LineInput implementation.
  void EnsureRawMode() override;
  void EnsureNoRawMode() override;

 protected:
  // LineInputEditor implementation.
  void Write(const std::string& str) override;

 private:
#if !defined(__Fuchsia__)
  // The terminal is converted into raw mode when the prompt is visible and
  // accepting input. Then it's switched back. This block tracks that
  // information. Use unique_ptr to avoid bringing all terminal headers into
  // this header.
  bool raw_mode_enabled_ = false;
  std::unique_ptr<termios> raw_termios_;
  std::unique_ptr<termios> original_termios_;
#endif
};

}  // namespace line_input

#endif  // SRC_LIB_LINE_INPUT_LINE_INPUT_H_
