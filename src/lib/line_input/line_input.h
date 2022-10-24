// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_LINE_INPUT_LINE_INPUT_H_
#define SRC_LIB_LINE_INPUT_LINE_INPUT_H_

#include <deque>
#include <map>
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
  static constexpr char kKeyControlN = 14;  // Down
  static constexpr char kKeyControlP = 16;  // Up
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
//  1. Call Show().
//  2. Push data to it via OnInput().
//  3. On an accept callback:
//     3a. Handle the input.
//     3b. Add line to history if desired.
//  4. Repeat until done.
//  5. Call Hide() to put the terminal back.
//
// If your application has data that it needs to print asynchronously, just:
//  1. Call Hide().
//  2. Print the stuff you want.
//  3. Call Show().
class LineInput {
 public:
  virtual ~LineInput() = default;

  // Called with the user input when the user acceps a line. This takes a copy because the accept
  // callback can often modify the command history which in turn owns the string being edited and
  // accepted.
  using AcceptCallback = fit::function<void(std::string)>;

  // Called when the current line changes. This is not called for <Enter> which doesn't change
  // anything but will call the AcceptCallback. As in AcceptCallback, this takes a copy.
  using ChangeCallback = fit::function<void(std::string)>;

  // Given some typing, returns a prioritized list of completions.
  using AutocompleteCallback = fit::function<std::vector<std::string>(const std::string&)>;

  // Callback that indicates Control-C or EOF (Control-D) was typed.
  using CancelCallback = fit::function<void()>;
  using EofCallback = fit::function<void()>;

  // Setup -----------------------------------------------------------------------------------------

  // Provides the callback for tab completion.
  virtual void SetAutocompleteCallback(AutocompleteCallback cb) = 0;

  // Provides the callback for when the current line changes.
  virtual void SetChangeCallback(ChangeCallback cb) = 0;

  // Provides the callback for handling Control-C. If unset, "^C" will be echoed and the line
  // will be cleared.
  virtual void SetCancelCallback(CancelCallback cb) = 0;

  // Provides the callback for handling EOF. If unset EOF will be ignored.
  virtual void SetEofCallback(EofCallback cb) = 0;

  // Sets the maximum width of a line. Beyond this the input will scroll. Setitng to 0 will disable
  // horizontal scrolling.
  virtual void SetMaxCols(size_t max) = 0;

  // Querying --------------------------------------------------------------------------------------

  // Returns the current input text.
  virtual const std::string& GetLine() const = 0;

  // Returns the current history. The most resent input is at the begin().
  virtual const std::deque<std::string>& GetHistory() const = 0;

  // State -----------------------------------------------------------------------------------------

  // Provides one character of input to the editor. Callbacks for autocomplete or line done will be
  // issued from within this function.
  virtual void OnInput(char c) = 0;

  // Adds the given line to history. If the history is longer than max_history_, the oldest thing
  // will be deleted.
  //
  // AddToHistory should be called on startup before the initial Show() call, or from within the
  // accept callback (typically you would add the current line at this point).
  virtual void AddToHistory(const std::string& line) = 0;

  // The input can be hidden and re-shown. Hiding it will erase the current line and put the cursor
  // at the beginning of the line, but not change any internal state. Showing it again will repaint
  // the line at the new cursor position. This allows other output to be printed to the screen
  // without interfering with the input.
  //
  // Hiding or showing when it's already in that state will do nothing. There is not a reference
  // count on the hide calls.
  //
  // OnInput() should not be called while hidden.
  //
  // Tip: When the application is done (the user types "quit" or whatever), call Hide() from within
  // the AcceptCallback or EofCallback. This will ensure the prompt isn't repainted when the
  // callback is complete only to be rehidden on exit (which will cause flickering).
  virtual void Hide() = 0;
  virtual void Show() = 0;

  // Replaces the contents of the current line with the given contents. The cursor will be placed
  // at the end of the text. Any changes in editing history will be reset.
  //
  // This bypasses handling of any special characters and input and any such
  // characters will be included as literals on the line. This will issue any changed callbacks.
  virtual void SetCurrentInput(const std::string& input) = 0;
};

// Implementation of LineInput that implements the editing state. Output is still abstract to
// allow for output to different places.
class LineInputEditor : public LineInput {
 public:
  explicit LineInputEditor(AcceptCallback accept_cb, const std::string& prompt);
  virtual ~LineInputEditor();

  // LineInput implementation.
  void SetAutocompleteCallback(AutocompleteCallback cb) override;
  void SetChangeCallback(ChangeCallback cb) override;
  void SetCancelCallback(CancelCallback cb) override;
  void SetEofCallback(EofCallback cb) override;
  void SetMaxCols(size_t max) override;
  const std::string& GetLine() const override;
  const std::deque<std::string>& GetHistory() const override;
  void OnInput(char c) override;
  void AddToHistory(const std::string& line) override;
  void Hide() override;
  void Show() override;
  void SetCurrentInput(const std::string& input) override;

  size_t pos() const { return pos_; }

  bool in_reverse_history_mode() const { return reverse_history_mode_; }
  size_t reverse_history_index() const { return reverse_history_index_; }

  // Exposed for testing purposes.
  std::string GetReverseHistoryPrompt() const;
  std::string GetReverseHistorySuggestion() const;

 protected:
  enum TerminalMode {
    // The original mode of the terminal when we started. Used for putting it back when we exit.
    kOriginalMode,

    // Raw input and output. Used for active line input.
    kRawInOutMode,

    // Raw input only. Output will be formatted as normal in a terminal. Used when the input is
    // hidden so the app may print with normal behavior but input is still raw. This prevents
    // echoing input to the terminal when we're not processing it, and Control-C from exiting the
    // app out from under us.
    kRawInMode,
  };

  // Sets the terminal mode to the given value if necessary.
  virtual void EnsureTerminalMode(TerminalMode mode) {}

  // Abstract output function, overridden by a derived class to output to screen.
  virtual void Write(const std::string& data) = 0;

  // Helper to return the current line of text as an editable string. This uses the overlay provided
  // by editing_history and falls back to promoting the value from persistent_history_.
  //
  // Use the const GetLine() on the base class to avoid the promotion if the returned value will
  // not be modified.
  std::string& mutable_cur_line() {
    if (auto found = editing_history_.find(history_index_); found != editing_history_.end())
      return found->second;

    // Make the permanent history line mutable by copying to the editing history.
    return editing_history_[history_index_] = persistent_history_[history_index_];
  }

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

  // Issues a line change notification and repaints the current line.
  void LineChanged();

  // Repaints the current line without issuing a line change notification.
  void RepaintLine();

  void ResetLineState();

  AcceptCallback accept_callback_;
  ChangeCallback change_callback_;  // Possibly null.
  std::string prompt_;

  size_t max_cols_ = 0;
  AutocompleteCallback autocomplete_callback_;  // Possibly null.
  CancelCallback cancel_callback_;              // Possibly null;.
  EofCallback eof_callback_;                    // Possibly null;.

  // Indicates whether the line is currently visible (as controlled by Show()/Hide()).
  bool visible_ = false;

  // The history is basically the line stack going back in time as indices increase. There are two
  // versions of history: the permanent history of stuff the user has typed, and the editing history
  // which is where temporary modifications are made until something is accepted.
  //
  // The editing history shadows the permanent history so the user can move up and down and edit
  // different items in history, seeing those edits as they move up and down. But when a new history
  // item is added, those edits are all cleared and the history represents what was actually input
  // at each point. Only the line that was input is kept, and that's saved only by adding it to the
  // front of history. This temporary shadow copy of history where edits occur matches the behavior
  // of other popular line input libraries.
  //
  // Use GetLine() to get a const view of the current line.  Use mutable_cur_line() to get a mutable
  // version of the current line (promotes to editable). Avoid changing the strings in these
  // structures outside of mutable_cur_line().
  std::deque<std::string> persistent_history_;  // front() is newest.
  size_t history_index_ = 0;                    // Offset from persistent_history_.front().
  const size_t max_history_ = 256;
  std::map<size_t, std::string> editing_history_;  // Indices override items in persistent_history_.

  bool completion_mode_ = false;
  std::vector<std::string> completions_;
  size_t completion_index_ = 0;

  // Tracks the current line's state before suggesting completions so we can
  // put them back if necessary. Only valid when completion_mode_ = true.
  std::string line_before_completion_;
  size_t pos_before_completion_ = 0;

  // When an escape is read, we enter "escaped input" mode which interprets the
  // next few characters input as an escape sequence.
  bool reading_escaped_input_ = false;
  std::string escape_sequence_;

  bool reverse_history_mode_ = false;
  std::string reverse_history_input_;

  // Index within history the reverse search suggestion current is. 0 means not found, as that is
  // pointing to the current line, which we don't want to do a history search in.
  size_t reverse_history_index_ = 0;

  size_t pos_ = 0;  // Current editing position.
};

// Implementation of LineInput that prints to stdout. The caller is still responsible for providing
// input asynchronously. The initial width of the output will be automatically derived from the
// terminal associated wit, as that is pointing to the current line, which we don't want to do a
// history search in. stdout (if any).
class LineInputStdout : public LineInputEditor {
 public:
  LineInputStdout(AcceptCallback accept_cb, const std::string& prompt);
  ~LineInputStdout() override;

 protected:
  // LineInputEditor implementation.
  void EnsureTerminalMode(TerminalMode mode) override;
  void Write(const std::string& str) override;

 private:
#if !defined(__Fuchsia__)
  // The terminal is converted into raw mode when the prompt is visible and accepting input. Then
  // it's switched back. This block tracks that information. Use unique_ptr to avoid bringing all
  // terminal headers into this header.
  TerminalMode terminal_mode_ = kOriginalMode;
  std::unique_ptr<termios> original_termios_;
  std::unique_ptr<termios> raw_inout_termios_;
  std::unique_ptr<termios> raw_in_termios_;
#endif
};

}  // namespace line_input

#endif  // SRC_LIB_LINE_INPUT_LINE_INPUT_H_
