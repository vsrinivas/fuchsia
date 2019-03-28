// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/line_input.h"

#include <stdio.h>
#include <unistd.h>

#ifdef __Fuchsia__
#include <fuchsia/hardware/pty/c/fidl.h>
#include <lib/fdio/io.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"

namespace zxdb {

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
    zx_status_t call_status = fuchsia_hardware_pty_DeviceGetWindowSize(
      fdio_unsafe_borrow_channel(io), &status, &wsz);
    fdio_unsafe_release(io);
    if (call_status != ZX_OK || status != ZX_OK) {
      return 0;
    }

    return wsz.width;
  }
#else
  struct winsize ws;
  if (ioctl(fileno, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    return ws.ws_col;
#endif
  return 0;  // 0 means disable scrolling.
}

}  // namespace

LineInputBase::LineInputBase(const std::string& prompt) : prompt_(prompt) {
  // Start with a blank item at [0] which is where editing will take place.
  history_.emplace_front();
}

LineInputBase::~LineInputBase() { EnsureNoRawMode(); }

void LineInputBase::BeginReadLine() {
  FXL_DCHECK(!editing_);  // Two BeginReadLine calls with no enter input.

  ResetLineState();
  RepaintLine();
}

bool LineInputBase::OnInput(char c) {
  FXL_DCHECK(editing_);  // BeginReadLine not called.
  FXL_DCHECK(visible_);  // Don't call while hidden.

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
    case SpecialCharacters::kKeyControlD:
      HandleEndOfFile();
      return true;
    case SpecialCharacters::kKeyControlE:
      MoveEnd();
      break;
    case SpecialCharacters::kKeyControlF:
      MoveRight();
      break;
    case SpecialCharacters::kKeyFormFeed:
      HandleFormFeed();
      break;
    case SpecialCharacters::kKeyTab:
      HandleTab();
      break;
    case SpecialCharacters::kKeyNewline:
    case SpecialCharacters::kKeyEnter:
      HandleEnter();
      return true;
    case SpecialCharacters::kKeyControlN:
      MoveDown();
      break;
    case SpecialCharacters::kKeyControlP:
      MoveUp();
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

void LineInputBase::AddToHistory(const std::string& line) {
  if (history_.size() == max_history_)
    history_.pop_back();

  // Editing takes place at history_[0], so this replaces it and pushes
  // everything else back with a new blank line to edit.
  history_[0] = line;
  history_.emplace_front();
}

void LineInputBase::Hide() {
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

void LineInputBase::Show() {
  FXL_DCHECK(!visible_);  // Show() called more than once.
  visible_ = true;
  if (!editing_)
    return;
  RepaintLine();
}

void LineInputBase::HandleEscapedInput(char c) {
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

void LineInputBase::HandleBackspace() {
  if (pos_ == 0)
    return;
  pos_--;
  cur_line().erase(pos_, 1);
  RepaintLine();
}

void LineInputBase::HandleDelete() {
  if (pos_ < cur_line().size()) {
    cur_line().erase(pos_, 1);
    RepaintLine();
  }
}

void LineInputBase::HandleEnter() {
  Write("\r\n");

  if (history_.size() == max_history_)
    history_.pop_back();
  std::string new_line = cur_line();
  history_[0] = new_line;
  EnsureNoRawMode();
  editing_ = false;
}

void LineInputBase::HandleTab() {
  if (!completion_callback_)
    return;  // Can't do completions.

  if (!completion_mode_) {
    completions_ = completion_callback_(cur_line());
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

void LineInputBase::HandleNegAck() {
  cur_line() = cur_line().substr(pos_);
  pos_ = 0;
  RepaintLine();
}

void LineInputBase::HandleEndOfTransimission() {
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

void LineInputBase::HandleEndOfFile() {
  eof_ = true;
  editing_ = false;
}

void LineInputBase::HandleFormFeed() {
  Write("\033c");  // Form feed.
  RepaintLine();
}

void LineInputBase::Insert(char c) {
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

void LineInputBase::MoveLeft() {
  if (pos_ > 0) {
    pos_--;
    RepaintLine();
  }
}

void LineInputBase::MoveRight() {
  if (pos_ < cur_line().size()) {
    pos_++;
    RepaintLine();
  }
}

void LineInputBase::MoveUp() {
  if (history_index_ < history_.size() - 1) {
    history_index_++;
    pos_ = cur_line().size();
    RepaintLine();
  }
}

void LineInputBase::MoveDown() {
  if (history_index_ > 0) {
    history_index_--;
    pos_ = cur_line().size();
    RepaintLine();
  }
}

void LineInputBase::MoveHome() {
  pos_ = 0;
  RepaintLine();
}

void LineInputBase::MoveEnd() {
  pos_ = cur_line().size();
  RepaintLine();
}

void LineInputBase::CancelCompletion() {
  cur_line() = line_before_completion_;
  pos_ = pos_before_completion_;
  completion_mode_ = false;
  completions_ = std::vector<std::string>();
  RepaintLine();
}

void LineInputBase::AcceptCompletion() {
  completion_mode_ = false;
  completions_ = std::vector<std::string>();
  // Line shouldn't need repainting since this doesn't update it.
}

void LineInputBase::RepaintLine() {
  EnsureRawMode();

  std::string buf;
  buf.reserve(64);

  buf += SpecialCharacters::kTermBeginningOfLine;

  // Only print up to max_cols_ - 1 to leave room for the cursor at the end.
  std::string line_data = prompt_ + cur_line();
  size_t pos_in_cols = prompt_.size() + pos_;
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
  snprintf(forward_buf, sizeof(forward_buf),
           SpecialCharacters::kTermCursorToColFormat,
           static_cast<int>(pos_in_cols));
  buf += forward_buf;

  Write(buf);
}

void LineInputBase::ResetLineState() {
  editing_ = true;
  pos_ = 0;
  history_index_ = 0;
  completion_mode_ = false;

  cur_line() = std::string();
}

LineInputStdout::LineInputStdout(const std::string& prompt)
    : LineInputBase(prompt) {
  set_max_cols(GetTerminalMaxCols(STDIN_FILENO));
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
  return line();
}

}  // namespace zxdb
