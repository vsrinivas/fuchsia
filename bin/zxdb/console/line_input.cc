// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/line_input.h"

#include <stdio.h>
#include <unistd.h>

namespace zxdb {

namespace {

constexpr char kKeyControlA = 1;
constexpr char kKeyControlB = 2;
constexpr char kKeyControlE = 5;
constexpr char kKeyControlF = 6;
constexpr char kKeyControlH = 8;
constexpr char kKeyTab = 9;
constexpr char kKeyNewline = 10;
constexpr char kKeyEnter = 13;
constexpr char kKeyControlN = 14;
constexpr char kKeyControlP = 16;
constexpr char kKeyEsc = 27;
constexpr char kKeyBackspace = 127;

// Escape sequences for terminal output.
const char kTermBeginningOfLine[] = "\r";
const char kTermClearToEnd[] = "\x1b[0K";
const char kTermCursorToColFormat[] = "\r\x1b[%dC";  // printf format.

}  // namespace

LineInputBase::LineInputBase(const std::string& prompt) : prompt_(prompt) {
}

LineInputBase::~LineInputBase() {
}

void LineInputBase::BeginReadLine() {
  pos_ = 0;
  history_index_ = 0;
  completion_mode_ = false;
  history_.emplace_front();

  cur_line() = std::string();
  RepaintLine();
}

bool LineInputBase::OnInput(char c) {
  if (reading_escaped_input_) {
    HandleEscapedInput(c);
    return false;
  }

  if (completion_mode_) {
    // Special keys for completion mode.
    if (c == kKeyTab) {
      HandleTab();
      return false;
    }
    // We don't handle escape here to cancel because that's ambiguous with
    // escape sequences like arrow keys.
    AcceptCompletion();
    // Fall through to normal key processing.
  }

  switch (c) {
    case kKeyControlA:
      MoveHome();
      break;
    case kKeyControlB:
      MoveLeft();
      break;
    case kKeyControlE:
      MoveEnd();
      break;
    case kKeyControlF:
      MoveRight();
      break;
    case kKeyTab:
      HandleTab();
      break;
    case kKeyNewline:
    case kKeyEnter:
      HandleEnter();
      return true;
    case kKeyControlN:
      MoveDown();
      break;
    case kKeyControlP:
      MoveUp();
      break;
    case kKeyEsc:
      reading_escaped_input_ = true;
      break;
    case kKeyControlH:
    case kKeyBackspace:
      HandleBackspace();
      break;
    default:
      Insert(c);
      break;
  }
  return false;
}

void LineInputBase::HandleEscapedInput(char c) {
  // Escape sequences are two bytes, buffer until we have both.
  escape_sequence_.push_back(c);
  if (escape_sequence_.size() < 2)
    return;

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
  Write(std::string(1, '\n'));

  if (history_.size() == max_history_)
    history_.pop_back();
  std::string new_line = cur_line();
  history_[0] = new_line;
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

void LineInputBase::Insert(char c) {
  if (pos_ == cur_line().size()) {
    // Append to end. Optimize output to avoid redrawing the entire line.
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
  std::string buf;
  buf.reserve(64);

  buf += kTermBeginningOfLine;
  buf += prompt_;
  buf += cur_line();
  buf += kTermClearToEnd;

  char forward_buf[16];
  snprintf(forward_buf, sizeof(forward_buf), kTermCursorToColFormat,
           static_cast<int>(prompt_.size() + pos_));
  buf += forward_buf;

  Write(buf);
}

LineInputStdout::LineInputStdout(const std::string& prompt)
    : LineInputBase(prompt) {}
LineInputStdout::~LineInputStdout() {}

void LineInputStdout::Write(const std::string& data) {
  write(STDOUT_FILENO, data.data(), data.size());
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
