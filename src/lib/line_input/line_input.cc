// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/line_input/line_input.h"

#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "src/lib/fxl/strings/split_string.h"

namespace line_input {

const char* SpecialCharacters::kTermBeginningOfLine = "\r";
const char* SpecialCharacters::kTermClearToEnd = "\x1b[0K";
const char* SpecialCharacters::kTermCursorToColFormat = "\r\x1b[%dC";

namespace {

size_t GetTerminalMaxCols(int fileno) {
  struct winsize ws;
  if (ioctl(fileno, TIOCGWINSZ, &ws) != -1)
    return ws.ws_col;
  return 0;  // 0 means disable scrolling.
}

}  // namespace

LineInputEditor::LineInputEditor(AcceptCallback accept_cb, const std::string& prompt)
    : accept_callback_(std::move(accept_cb)), prompt_(prompt) {
  // Start with a blank item at [0] which is where editing will take place.
  persistent_history_.emplace_front();
}

LineInputEditor::~LineInputEditor() {
  // Note: the terminal is put back in ~LineInputStdout since we can't call the overridden
  // EnsureTerminalMode() on our already-destructed derived class.
}

void LineInputEditor::SetAutocompleteCallback(AutocompleteCallback cb) {
  autocomplete_callback_ = std::move(cb);
}

void LineInputEditor::SetChangeCallback(ChangeCallback cb) { change_callback_ = std::move(cb); }

void LineInputEditor::SetCancelCallback(CancelCallback cb) { cancel_callback_ = std::move(cb); }

void LineInputEditor::SetEofCallback(EofCallback cb) { eof_callback_ = std::move(cb); }

void LineInputEditor::SetMaxCols(size_t max) { max_cols_ = max; }

const std::string& LineInputEditor::GetLine() const {
  if (auto found = editing_history_.find(history_index_); found != editing_history_.end())
    return found->second;
  return persistent_history_[history_index_];
}

const std::deque<std::string>& LineInputEditor::GetHistory() const { return persistent_history_; }

void LineInputEditor::OnInput(char c) {
  FX_DCHECK(visible_);  // Don't call while hidden.

  // Reverse history mode does its own input handling.
  if (reverse_history_mode_) {
    HandleReverseHistory(c);
    return;
  }

  if (reading_escaped_input_) {
    HandleEscapedInput(c);
    return;
  }

  if (completion_mode_) {
    // Special keys for completion mode.
    if (c == SpecialCharacters::kKeyTab) {
      HandleTab();
      return;
    }
    // We don't handle escape here to cancel because that's ambiguous with
    // escape sequences like arrow keys.
    AcceptCompletion();
    // Fall through to normal key processing.
  }

  switch (c) {
    case SpecialCharacters::kKeyControlA:
      MoveHome();
      break;
    case SpecialCharacters::kKeyControlB:
      MoveLeft();
      break;
    case SpecialCharacters::kKeyControlC:
      CancelCommand();
      break;
    case SpecialCharacters::kKeyControlD:
      if (GetLine().empty()) {
        HandleEndOfFile();
        return;
      } else {
        HandleDelete();
      }
      break;
    case SpecialCharacters::kKeyControlE:
      MoveEnd();
      break;
    case SpecialCharacters::kKeyControlF:
      MoveRight();
      break;
    case SpecialCharacters::kKeyControlK:
      DeleteToEnd();
      break;
    case SpecialCharacters::kKeyFormFeed:
      HandleFormFeed();
      break;
    case SpecialCharacters::kKeyTab:
      HandleTab();
      break;
    case SpecialCharacters::kKeyNewline:  // == Ctrl + J
    case SpecialCharacters::kKeyEnter:    // == Ctrl + M
      HandleEnter();
      return;
    case SpecialCharacters::kKeyControlN:
      MoveDown();
      break;
    case SpecialCharacters::kKeyControlP:
      MoveUp();
      break;
    case SpecialCharacters::kKeyControlR:
      StartReverseHistoryMode();
      break;
    case SpecialCharacters::kKeyControlT:
      TransposeLastTwoCharacters();
      break;
    case SpecialCharacters::kKeyControlU:
      HandleNegAck();
      break;
    case SpecialCharacters::kKeyControlW:
      HandleEndOfTransimission();
      break;
    case SpecialCharacters::kKeyEsc:
      reading_escaped_input_ = true;
      break;
    case SpecialCharacters::kKeyControlH:
    case SpecialCharacters::kKeyBackspace:
      HandleBackspace();
      break;
    default:
      Insert(c);
      break;
  }
}

void LineInputEditor::AddToHistory(const std::string& line) {
  if (line.empty())
    return;

  if (persistent_history_.size() > 1 && persistent_history_[1] == line)
    return;

  if (persistent_history_.size() == max_history_)
    persistent_history_.pop_back();

  // Editing takes place at index 0, so this replaces it and pushes everything else back with a new
  // blank line to edit.
  persistent_history_[0] = line;
  persistent_history_.emplace_front();
  editing_history_.clear();
}

void LineInputEditor::Hide() {
  if (!visible_)
    return;  // Already hidden.
  visible_ = false;

  std::string cmd;
  cmd += SpecialCharacters::kTermBeginningOfLine;
  cmd += SpecialCharacters::kTermClearToEnd;

  Write(cmd);
  EnsureTerminalMode(kRawInMode);
}

void LineInputEditor::Show() {
  if (visible_)
    return;  // Already shown.
  visible_ = true;
  RepaintLine();
}

void LineInputEditor::SetCurrentInput(const std::string& input) {
  editing_history_.clear();
  history_index_ = 0;

  mutable_cur_line() = input;
  pos_ = input.size();
  completion_mode_ = false;

  LineChanged();
}

void LineInputEditor::HandleEscapedInput(char c) {
  // Escape sequences are two bytes, buffer until we have both.
  escape_sequence_.push_back(c);
  if (escape_sequence_.size() < 2)
    return;

  if (escape_sequence_.size() < 3 && escape_sequence_[0] == '[' && escape_sequence_[1] >= '0' &&
      escape_sequence_[1] <= '9') {
    // This is a three-character escape sequence but we've only received two. Wait for more.
    return;
  }

  // Clear the escaped state before running any functions. Some of them can change the input
  // which can in turn issue callbacks which can cause other stuff to happen, and we want to be
  // in a fresh state if it does.
  reading_escaped_input_ = false;
  std::string sequence = escape_sequence_;
  escape_sequence_.clear();

  // See https://en.wikipedia.org/wiki/ANSI_escape_code for escape codes.
  if (sequence[0] == '[') {
    if (sequence[1] >= '0' && sequence[1] <= '9') {
      // 3-character extended sequence.
      if (sequence.size() < 3)
        return;  // Wait for another character.
      if (sequence[1] == '3' && sequence[2] == '~') {
        HandleDelete();
      } else if (sequence[1] == '1' && sequence[2] == '~') {
        MoveHome();
      } else if (sequence[1] == '4' && sequence[2] == '~') {
        MoveEnd();
      }
    } else {
      // Two-character '[' sequence.
      switch (sequence[1]) {
        case 'A':
          MoveUp();
          break;
        case 'B':
          MoveDown();
          break;
        case 'C':
          MoveRight();
          break;
        case 'D':
          MoveLeft();
          break;
        case 'H':
          MoveHome();
          break;
        case 'F':
          MoveEnd();
          break;
      }
    }
  } else if (sequence[0] == '0') {
    switch (sequence[1]) {
      case 'H':
        MoveHome();
        break;
      case 'F':
        MoveEnd();
        break;
    }
  }
}

void LineInputEditor::HandleBackspace() {
  if (pos_ == 0)
    return;
  pos_--;
  mutable_cur_line().erase(pos_, 1);
  LineChanged();
}

void LineInputEditor::HandleDelete() {
  std::string& line = mutable_cur_line();
  if (pos_ < line.size()) {
    line.erase(pos_, 1);
    LineChanged();
  }
}

void LineInputEditor::HandleEnter() {
  Write("\r\n");

  if (persistent_history_.size() == max_history_)
    persistent_history_.pop_back();
  std::string new_line = GetLine();
  persistent_history_[0] = new_line;
  EnsureTerminalMode(kRawInMode);

  accept_callback_(GetLine());

  ResetLineState();
  if (visible_)
    RepaintLine();
}

void LineInputEditor::HandleTab() {
  if (!autocomplete_callback_)
    return;  // Can't do completions.

  if (!completion_mode_) {
    completions_ = autocomplete_callback_(GetLine());
    completion_index_ = 0;
    if (completions_.empty())
      return;  // No completions, don't enter completion mode.

    // Transition to tab completion mode.
    completion_mode_ = true;
    line_before_completion_ = GetLine();
    pos_before_completion_ = pos_;

    // Put the current line at the end of the completion stack so tabbing
    // through wraps around to it.
    completions_.push_back(line_before_completion_);
  } else {
    // Advance to the next completion, with wraparound.
    completion_index_++;
    if (completion_index_ == completions_.size())
      completion_index_ = 0;
  }

  // Show the new completion.
  std::string& line = mutable_cur_line();
  line = completions_[completion_index_];
  pos_ = line.size();
  LineChanged();
}

void LineInputEditor::HandleNegAck() {
  std::string& line = mutable_cur_line();
  line = line.substr(pos_);
  pos_ = 0;
  LineChanged();
}

// This is used to delete the previous word (Control-W).
void LineInputEditor::HandleEndOfTransimission() {
  std::string& line = mutable_cur_line();
  if (line.empty())
    return;

  // Delete the characters before the cursor following the pattern "<nonspace>*<space>*"
  size_t begin_delete = pos_;
  while (begin_delete > 0 && line[begin_delete - 1] == ' ')
    begin_delete--;
  while (begin_delete > 0 && line[begin_delete - 1] != ' ')
    begin_delete--;

  line.erase(line.begin() + begin_delete, line.begin() + pos_);
  pos_ = begin_delete;
  LineChanged();
}

void LineInputEditor::HandleEndOfFile() {
  Write("\r\n");
  if (eof_callback_)
    eof_callback_();

  ResetLineState();
  if (visible_)
    LineChanged();
}

void LineInputEditor::HandleReverseHistory(char c) {
  if (reading_escaped_input_) {
    // Escape sequences are two bytes, buffer until we have both.
    escape_sequence_.push_back(c);
    if (escape_sequence_.size() < 2)
      return;

    if (escape_sequence_[0] == '[') {
      if (escape_sequence_[1] >= '0' && escape_sequence_[1] <= '9') {
        // 3-character extended sequence.
        if (escape_sequence_.size() < 3)
          return;  // Wait for another character.
      }
    }

    // Any other escape sequence exists reverse history mode.
    EndReverseHistoryMode(false);
  }

  // Only a handful of operations are valid in reverse history mode.
  switch (c) {
    // Enters selects the current suggestion.
    case SpecialCharacters::kKeyEnter:
    case SpecialCharacters::kKeyNewline:
      EndReverseHistoryMode(true);
      break;
    // ctrl-r again searches for the next match.
    case SpecialCharacters::kKeyControlR:
      SearchNextReverseHistory(false);
      break;
    // Deleting a character starts the search anew.
    case SpecialCharacters::kKeyControlH:
    case SpecialCharacters::kKeyBackspace:
      if (!reverse_history_input_.empty())
        reverse_history_input_.resize(reverse_history_input_.size() - 1);
      SearchNextReverseHistory(true);
      break;
    // Almost all special characters end history mode. This is what sh does.
    case SpecialCharacters::kKeyControlA:
    case SpecialCharacters::kKeyControlB:
    case SpecialCharacters::kKeyControlC:
    case SpecialCharacters::kKeyControlD:
    case SpecialCharacters::kKeyControlE:
    case SpecialCharacters::kKeyControlF:
    case SpecialCharacters::kKeyFormFeed:
    case SpecialCharacters::kKeyTab:
    case SpecialCharacters::kKeyControlN:
    case SpecialCharacters::kKeyControlP:
    case SpecialCharacters::kKeyControlU:
    case SpecialCharacters::kKeyControlW:
    case SpecialCharacters::kKeyEsc:
      EndReverseHistoryMode(false);
      break;
    // Add the input to the current search string and do the lookup anew.
    default:
      reverse_history_input_.append(1, c);
      SearchNextReverseHistory(true);
      break;
  }

  LineChanged();
}

void LineInputEditor::StartReverseHistoryMode() {
  FX_DCHECK(!reverse_history_mode_);
  reverse_history_mode_ = true;
  reverse_history_index_ = 0;
  reverse_history_input_.clear();

  LineChanged();
}

void LineInputEditor::EndReverseHistoryMode(bool accept_suggestion) {
  FX_DCHECK(reverse_history_mode_);
  reverse_history_mode_ = false;

  if (accept_suggestion) {
    std::string& line = mutable_cur_line();
    line = GetReverseHistorySuggestion();
    pos_ = line.size();
  } else {
    pos_ = 0;
  }
}

void LineInputEditor::SearchNextReverseHistory(bool restart) {
  if (restart) {
    reverse_history_index_ = 0;
  } else {
    // We want to find the *next* suggestion after the current one.
    reverse_history_index_++;
  }

  // No input, no search.
  if (reverse_history_input_.empty()) {
    pos_ = 0;
    return;
  }

  // Search for a history entry that has the input a a substring.
  size_t index = reverse_history_index_ == 0 ? 1 : reverse_history_index_;
  for (size_t i = index; i < persistent_history_.size(); i++) {
    const std::string& line = persistent_history_[i];
    auto cursor_offset = line.find(reverse_history_input_);
    if (cursor_offset == std::string::npos)
      continue;

    // We found a suggestion.
    reverse_history_index_ = i;
    pos_ = cursor_offset;
    return;
  }

  // If we didn't find a suggestion, we reset the state and clear the state, to indicate to the user
  // that it rolled over or it didn't find anything.
  reverse_history_index_ = 0;
  pos_ = 0;
}

void LineInputEditor::HandleFormFeed() {
  Write("\033c");  // Form feed.
  LineChanged();
}

void LineInputEditor::Insert(char c) {
  std::string& line = mutable_cur_line();
  if (pos_ == line.size() && (max_cols_ == 0 || line.size() + prompt_.size() < max_cols_ - 1)) {
    // Append to end and no scrolling needed. Optimize output to avoid
    // redrawing the entire line.
    line.push_back(c);
    pos_++;
    Write(std::string(1, c));
    if (change_callback_)
      change_callback_(line);
  } else {
    // Insert in the middle.
    line.insert(pos_, 1, c);
    pos_++;
    LineChanged();
  }
}

void LineInputEditor::MoveLeft() {
  if (pos_ > 0) {
    pos_--;
    RepaintLine();
  }
}

void LineInputEditor::MoveRight() {
  if (pos_ < GetLine().size()) {
    pos_++;
    RepaintLine();
  }
}

void LineInputEditor::MoveUp() {
  if (history_index_ < persistent_history_.size() - 1) {
    history_index_++;
    pos_ = GetLine().size();
    RepaintLine();
  }
}

void LineInputEditor::MoveDown() {
  if (history_index_ > 0) {
    history_index_--;
    pos_ = GetLine().size();
    RepaintLine();
  }
}

void LineInputEditor::MoveHome() {
  pos_ = 0;
  RepaintLine();
}

void LineInputEditor::MoveEnd() {
  pos_ = GetLine().size();
  RepaintLine();
}

void LineInputEditor::TransposeLastTwoCharacters() {
  if (pos_ >= 2) {
    std::string& line = mutable_cur_line();
    auto swap = line[pos_ - 1];
    line[pos_ - 1] = line[pos_ - 2];
    line[pos_ - 2] = swap;
    LineChanged();
  }
}

void LineInputEditor::CancelCommand() {
  if (cancel_callback_) {
    cancel_callback_();
  } else {
    Write("^C\r\n");
    ResetLineState();
    LineChanged();
  }
}

void LineInputEditor::DeleteToEnd() {
  std::string& line = mutable_cur_line();
  if (pos_ != line.size()) {
    line.resize(pos_);
    LineChanged();
  }
}

void LineInputEditor::CancelCompletion() {
  mutable_cur_line() = line_before_completion_;
  pos_ = pos_before_completion_;
  completion_mode_ = false;
  completions_ = std::vector<std::string>();
  LineChanged();
}

void LineInputEditor::AcceptCompletion() {
  completion_mode_ = false;
  completions_ = std::vector<std::string>();
  // Line shouldn't need repainting since this doesn't update it.
}

void LineInputEditor::LineChanged() {
  RepaintLine();
  if (change_callback_)
    change_callback_(GetLine());
}

void LineInputEditor::RepaintLine() {
  std::string prompt, line_data;
  if (!reverse_history_mode_) {
    prompt = prompt_;
    line_data = prompt + GetLine();
  } else {
    prompt = GetReverseHistoryPrompt();
    line_data = prompt + GetReverseHistorySuggestion();
  }

  EnsureTerminalMode(kRawInOutMode);

  std::string buf;
  buf.reserve(64);
  buf += SpecialCharacters::kTermBeginningOfLine;

  // Only print up to max_cols_ - 1 to leave room for the cursor at the end.
  size_t pos_in_cols = prompt.size() + pos_;
  if (max_cols_ > 0 && line_data.size() >= max_cols_ - 1) {
    // Needs scrolling. This code scrolls both the user entry and the prompt.
    // This avoids some edge cases where the prompt is wider than the screen.
    if (pos_in_cols < max_cols_) {
      // Cursor is on the screen with no scrolling, just trim from the right.
      line_data.resize(max_cols_);
    } else {
      // Cursor requires scrolling, position the cursor on the right.
      line_data = line_data.substr(pos_in_cols - max_cols_ + 1, max_cols_);
      pos_in_cols = max_cols_ - 1;
    }
    buf += line_data;
  } else {
    buf += line_data;
  }

  buf += SpecialCharacters::kTermClearToEnd;

  char forward_buf[32];
  snprintf(forward_buf, sizeof(forward_buf), SpecialCharacters::kTermCursorToColFormat,
           static_cast<int>(pos_in_cols));
  buf += forward_buf;

  Write(buf);
}

std::string LineInputEditor::GetReverseHistoryPrompt() const {
  std::string buf;
  buf.reserve(64);

  buf += "(reverse-i-search)'";
  buf += reverse_history_input_;
  buf += "': ";

  return buf;
}

std::string LineInputEditor::GetReverseHistorySuggestion() const {
  if (reverse_history_input_.empty())
    return {};

  if (reverse_history_index_ == 0 || reverse_history_index_ >= persistent_history_.size())
    return {};

  return persistent_history_[reverse_history_index_];
}

void LineInputEditor::ResetLineState() {
  pos_ = 0;
  history_index_ = 0;
  completion_mode_ = false;

  mutable_cur_line() = std::string();
}

// LineInputStdout ---------------------------------------------------------------------------------

LineInputStdout::LineInputStdout(AcceptCallback accept_cb, const std::string& prompt)
    : LineInputEditor(std::move(accept_cb), prompt) {
  SetMaxCols(GetTerminalMaxCols(STDIN_FILENO));
}
LineInputStdout::~LineInputStdout() { EnsureTerminalMode(kOriginalMode); }

void LineInputStdout::Write(const std::string& data) {
  write(STDOUT_FILENO, data.data(), data.size());
}

void LineInputStdout::EnsureTerminalMode(TerminalMode mode) {
#if !defined(__Fuchsia__)
  if (mode == terminal_mode_ || !isatty(STDOUT_FILENO))
    return;  // Nothing to do.

  terminal_mode_ = mode;

  // Synchronize with the buffered stdio stream.
  fflush(stdout);

  // Lazy initialize the terminal settings.
  if (!raw_inout_termios_) {
    original_termios_ = std::make_unique<termios>();
    if (tcgetattr(STDOUT_FILENO, original_termios_.get()) == -1)
      return;

    raw_inout_termios_ = std::make_unique<termios>(*original_termios_);
    raw_inout_termios_->c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw_inout_termios_->c_oflag &= ~(OPOST);
    raw_inout_termios_->c_oflag |= OCRNL;
    raw_inout_termios_->c_cflag |= CS8;
    raw_inout_termios_->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw_inout_termios_->c_cc[VMIN] = 1;
    raw_inout_termios_->c_cc[VTIME] = 0;

    // We don't want to actually use the original values for the output flags in "raw input" mode
    // because the terminal could have been in a bad state when we started the program. Instead, set
    // the flags specifically for normal output.
    raw_in_termios_ = std::make_unique<termios>(*raw_inout_termios_);
    raw_in_termios_->c_oflag = OPOST | ONLCR;
  }

  // Use TCSADRAIN to ensure that all output currently in the buffer has been written in the
  // expected manner. Don't use TCSAFLUSH because that will flush the input buffer which will
  // discard any user input that hasn't been read yet.
  switch (mode) {
    case kOriginalMode:
      tcsetattr(STDOUT_FILENO, TCSADRAIN, original_termios_.get());
      break;
    case kRawInOutMode:
      tcsetattr(STDOUT_FILENO, TCSADRAIN, raw_inout_termios_.get());
      break;
    case kRawInMode:
      tcsetattr(STDOUT_FILENO, TCSADRAIN, raw_in_termios_.get());
      break;
  }
#endif
}

}  // namespace line_input
