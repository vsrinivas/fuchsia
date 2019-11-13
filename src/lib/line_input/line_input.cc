// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/line_input/line_input.h"

#include <stdio.h>
#include <unistd.h>

#ifdef __Fuchsia__
#include <fuchsia/hardware/pty/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/split_string.h"

namespace line_input {

const char* SpecialCharacters::kTermBeginningOfLine = "\r";
const char* SpecialCharacters::kTermClearToEnd = "\x1b[0K";
const char* SpecialCharacters::kTermCursorToColFormat = "\r\x1b[%dC";

namespace {

size_t GetTerminalMaxCols(int fileno) {
#ifdef __Fuchsia__
  if (isatty(STDIN_FILENO)) {
    fdio_t* io = fdio_unsafe_fd_to_io(STDIN_FILENO);
    fuchsia_hardware_pty_WindowSize wsz;
    zx_status_t status;
    zx_status_t call_status =
        fuchsia_hardware_pty_DeviceGetWindowSize(fdio_unsafe_borrow_channel(io), &status, &wsz);
    fdio_unsafe_release(io);
    if (call_status != ZX_OK || status != ZX_OK) {
      return 0;
    }

    return wsz.width;
  }
#else
  struct winsize ws;
  if (ioctl(fileno, TIOCGWINSZ, &ws) != -1)
    return ws.ws_col;
#endif
  return 0;  // 0 means disable scrolling.
}

}  // namespace

LineInputEditor::LineInputEditor(const std::string& prompt) : prompt_(prompt) {
  // Start with a blank item at [0] which is where editing will take place.
  history_.emplace_front();
}

LineInputEditor::~LineInputEditor() { EnsureNoRawMode(); }

void LineInputEditor::SetAutocompleteCallback(AutocompleteCallback cb) {
  autocomplete_callback_ = std::move(cb);
}

void LineInputEditor::SetMaxCols(size_t max) { max_cols_ = max; }

bool LineInputEditor::IsEof() const { return eof_; }

const std::string& LineInputEditor::GetLine() const { return history_[history_index_]; }

const std::deque<std::string>& LineInputEditor::GetHistory() const { return history_; }

void LineInputEditor::BeginReadLine() {
  FXL_DCHECK(!editing_);  // Two BeginReadLine calls with no enter input.

  ResetLineState();
  RepaintLine();
}

bool LineInputEditor::OnInput(char c) {
  FXL_DCHECK(editing_);  // BeginReadLine not called.
  FXL_DCHECK(visible_);  // Don't call while hidden.

  // Reverse history mode does its own input handling.
  if (reverse_history_mode_) {
    HandleReverseHistory(c);
    return false;
  }

  if (reading_escaped_input_) {
    HandleEscapedInput(c);
    return false;
  }

  if (completion_mode_) {
    // Special keys for completion mode.
    if (c == SpecialCharacters::kKeyTab) {
      HandleTab();
      return false;
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
      if (cur_line().empty()) {
        HandleEndOfFile();
        return true;
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
      return true;
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
  return false;
}

void LineInputEditor::AddToHistory(const std::string& line) {
  if (line.empty())
    return;

  if (history_.size() > 1 && history_[1] == line)
    return;

  if (history_.size() == max_history_)
    history_.pop_back();

  // Editing takes place at history_[0], so this replaces it and pushes
  // everything else back with a new blank line to edit.
  history_[0] = line;
  history_.emplace_front();
}

void LineInputEditor::Hide() {
  FXL_DCHECK(visible_);  // Hide() called more than once.
  visible_ = false;

  if (!editing_)
    return;

  std::string cmd;
  cmd += SpecialCharacters::kTermBeginningOfLine;
  cmd += SpecialCharacters::kTermClearToEnd;

  Write(cmd);
  EnsureNoRawMode();
}

void LineInputEditor::Show() {
  FXL_DCHECK(!visible_);  // Show() called more than once.
  visible_ = true;
  if (!editing_)
    return;
  RepaintLine();
}

void LineInputEditor::HandleEscapedInput(char c) {
  // Escape sequences are two bytes, buffer until we have both.
  escape_sequence_.push_back(c);
  if (escape_sequence_.size() < 2)
    return;

  // See https://en.wikipedia.org/wiki/ANSI_escape_code for escape codes.
  if (escape_sequence_[0] == '[') {
    if (escape_sequence_[1] >= '0' && escape_sequence_[1] <= '9') {
      // 3-character extended sequence.
      if (escape_sequence_.size() < 3)
        return;  // Wait for another character.
      if (escape_sequence_[1] == '3' && escape_sequence_[2] == '~')
        HandleDelete();
      else if (escape_sequence_[1] == '1' && escape_sequence_[2] == '~')
        MoveHome();
      else if (escape_sequence_[1] == '4' && escape_sequence_[2] == '~')
        MoveEnd();
    } else {
      // Two-character '[' sequence.
      switch (escape_sequence_[1]) {
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
  } else if (escape_sequence_[0] == '0') {
    switch (escape_sequence_[1]) {
      case 'H':
        MoveHome();
        break;
      case 'F':
        MoveEnd();
        break;
    }
  }

  reading_escaped_input_ = false;
  escape_sequence_.clear();
}

void LineInputEditor::HandleBackspace() {
  if (pos_ == 0)
    return;
  pos_--;
  cur_line().erase(pos_, 1);
  RepaintLine();
}

void LineInputEditor::HandleDelete() {
  if (pos_ < cur_line().size()) {
    cur_line().erase(pos_, 1);
    RepaintLine();
  }
}

void LineInputEditor::HandleEnter() {
  Write("\r\n");

  if (history_.size() == max_history_)
    history_.pop_back();
  std::string new_line = cur_line();
  history_[0] = new_line;
  EnsureNoRawMode();
  editing_ = false;
}

void LineInputEditor::HandleTab() {
  if (!autocomplete_callback_)
    return;  // Can't do completions.

  if (!completion_mode_) {
    completions_ = autocomplete_callback_(cur_line());
    completion_index_ = 0;
    if (completions_.empty())
      return;  // No completions, don't enter completion mode.

    // Transition to tab completion mode.
    completion_mode_ = true;
    line_before_completion_ = cur_line();
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
  cur_line() = completions_[completion_index_];
  pos_ = cur_line().size();
  RepaintLine();
}

void LineInputEditor::HandleNegAck() {
  cur_line() = cur_line().substr(pos_);
  pos_ = 0;
  RepaintLine();
}

void LineInputEditor::HandleEndOfTransimission() {
  const auto& line = cur_line();
  if (line.empty())
    return;

  // We search for the last space that's before the cursor.
  size_t latest_space = 0;
  for (size_t i = 0; i < line.size(); i++) {
    if (i >= pos_)
      break;

    if (line[i] == ' ')
      latest_space = i;
  }

  // Ctrl-w removes from the latest space until the cursor.
  std::string new_line;
  if (latest_space > 0)
    new_line.append(line.substr(0, latest_space + 1));
  new_line.append(line.substr(pos_));

  size_t diff = line.size() - new_line.size();
  pos_ -= diff;
  cur_line() = std::move(new_line);
  RepaintLine();
}

void LineInputEditor::HandleEndOfFile() {
  eof_ = true;
  editing_ = false;
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

  RepaintLine();
};

void LineInputEditor::StartReverseHistoryMode() {
  FXL_DCHECK(!reverse_history_mode_);
  reverse_history_mode_ = true;
  reverse_history_index_ = 0;
  reverse_history_input_.clear();

  RepaintLine();
}

void LineInputEditor::EndReverseHistoryMode(bool accept_suggestion) {
  FXL_DCHECK(reverse_history_mode_);
  reverse_history_mode_ = false;

  if (accept_suggestion) {
    cur_line() = GetReverseHistorySuggestion();
    pos_ = cur_line().size();
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
  for (size_t i = index; i < history_.size(); i++) {
    const std::string& line = history_[i];
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
  RepaintLine();
}

void LineInputEditor::Insert(char c) {
  if (pos_ == cur_line().size() &&
      (max_cols_ == 0 || cur_line().size() + prompt_.size() < max_cols_ - 1)) {
    // Append to end and no scrolling needed. Optimize output to avoid
    // redrawing the entire line.
    cur_line().push_back(c);
    pos_++;
    Write(std::string(1, c));
  } else {
    // Insert in the middle.
    cur_line().insert(pos_, 1, c);
    pos_++;
    RepaintLine();
  }
}

void LineInputEditor::MoveLeft() {
  if (pos_ > 0) {
    pos_--;
    RepaintLine();
  }
}

void LineInputEditor::MoveRight() {
  if (pos_ < cur_line().size()) {
    pos_++;
    RepaintLine();
  }
}

void LineInputEditor::MoveUp() {
  if (history_index_ < history_.size() - 1) {
    history_index_++;
    pos_ = cur_line().size();
    RepaintLine();
  }
}

void LineInputEditor::MoveDown() {
  if (history_index_ > 0) {
    history_index_--;
    pos_ = cur_line().size();
    RepaintLine();
  }
}

void LineInputEditor::MoveHome() {
  pos_ = 0;
  RepaintLine();
}

void LineInputEditor::MoveEnd() {
  pos_ = cur_line().size();
  RepaintLine();
}

void LineInputEditor::TransposeLastTwoCharacters() {
  if (pos_ >= 2) {
    auto swap = cur_line()[pos_ - 1];
    cur_line()[pos_ - 1] = cur_line()[pos_ - 2];
    cur_line()[pos_ - 2] = swap;
    RepaintLine();
  }
}

void LineInputEditor::CancelCommand() {
  Write("^C\r\n");
  ResetLineState();
  RepaintLine();
}

void LineInputEditor::DeleteToEnd() {
  cur_line().resize(pos_);
  RepaintLine();
}

void LineInputEditor::CancelCompletion() {
  cur_line() = line_before_completion_;
  pos_ = pos_before_completion_;
  completion_mode_ = false;
  completions_ = std::vector<std::string>();
  RepaintLine();
}

void LineInputEditor::AcceptCompletion() {
  completion_mode_ = false;
  completions_ = std::vector<std::string>();
  // Line shouldn't need repainting since this doesn't update it.
}

void LineInputEditor::RepaintLine() {
  std::string prompt, line_data;
  if (!reverse_history_mode_) {
    prompt = prompt_;
    line_data = prompt + cur_line();
  } else {
    prompt = GetReverseHistoryPrompt();
    line_data = prompt + GetReverseHistorySuggestion();
  }

  EnsureRawMode();

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

  buf += "(reverse-i-search)`";
  buf += reverse_history_input_;
  buf += "': ";

  return buf;
}

std::string LineInputEditor::GetReverseHistorySuggestion() const {
  if (reverse_history_input_.empty())
    return {};

  if (reverse_history_index_ == 0 || reverse_history_index_ >= history_.size())
    return {};

  return history_[reverse_history_index_];
}

void LineInputEditor::ResetLineState() {
  editing_ = true;
  pos_ = 0;
  eof_ = false;
  history_index_ = 0;
  completion_mode_ = false;

  cur_line() = std::string();
}

// LineInputStdout ---------------------------------------------------------------------------------

LineInputStdout::LineInputStdout(const std::string& prompt) : LineInputEditor(prompt) {
  SetMaxCols(GetTerminalMaxCols(STDIN_FILENO));
}
LineInputStdout::~LineInputStdout() {}

void LineInputStdout::Write(const std::string& data) {
  write(STDOUT_FILENO, data.data(), data.size());
}

void LineInputStdout::EnsureRawMode() {
#if !defined(__Fuchsia__)
  if (raw_mode_enabled_)
    return;

  if (!raw_termios_) {
    if (!isatty(STDOUT_FILENO))
      return;

    // Don't commit until everything succeeds.
    original_termios_ = std::make_unique<termios>();
    if (tcgetattr(STDOUT_FILENO, original_termios_.get()) == -1)
      return;

    // Always expect non-raw node to wrap lines for us. Without this, if
    // somebody's terminal was left in raw mode when they started the debugger,
    // the non-interactive input will be wrapped incorrectly.
    original_termios_->c_oflag |= OPOST;

    raw_termios_ = std::make_unique<termios>(*original_termios_);

    raw_termios_->c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw_termios_->c_oflag &= ~(OPOST);
    raw_termios_->c_oflag |= OCRNL;
    raw_termios_->c_cflag |= CS8;
    raw_termios_->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw_termios_->c_cc[VMIN] = 1;
    raw_termios_->c_cc[VTIME] = 0;
  }

  fflush(stdout);  // Synchronize with the buffered stdio stream.
  if (tcsetattr(STDOUT_FILENO, TCSAFLUSH, raw_termios_.get()) < 0)
    return;

  raw_mode_enabled_ = true;
#endif
}

void LineInputStdout::EnsureNoRawMode() {
#if !defined(__Fuchsia__)
  if (raw_mode_enabled_) {
    fflush(stdout);  // Synchronize with the buffered stdio stream.
    tcsetattr(STDOUT_FILENO, TCSAFLUSH, original_termios_.get());
    raw_mode_enabled_ = false;
  }
#endif
}

LineInputBlockingStdio::LineInputBlockingStdio(const std::string& prompt)
    : LineInputStdout(prompt) {}

std::string LineInputBlockingStdio::ReadLine() {
  BeginReadLine();

  char read_buf;
  while (read(STDIN_FILENO, &read_buf, 1) == 1) {
    if (OnInput(read_buf))
      break;
  }
  return GetLine();
}

}  // namespace line_input
