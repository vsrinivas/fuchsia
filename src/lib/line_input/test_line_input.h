// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_LINE_INPUT_TEST_LINE_INPUT_H_
#define SRC_LIB_LINE_INPUT_TEST_LINE_INPUT_H_

#include "src/lib/line_input/line_input.h"

namespace line_input {

// An implementation of the single-line editor that just saves the input instead of writing anything
// to the screen. Used for tests.
class TestLineInput : public LineInputEditor {
 public:
  // This class always stores the accept data and optionally adds it to history, then calls the
  // accept callback if it's specified. Unlike the normal LineInput, the accept callback may be
  // null.
  TestLineInput(const std::string& prompt, AcceptCallback accept_cb = AcceptCallback())
      : LineInputEditor(
            [this](const std::string& s) {
              if (accept_goes_to_history_)
                AddToHistory(s);
              accept_ = s;
              if (accept_cb_)
                accept_cb_(s);
            },
            prompt),
        accept_cb_(std::move(accept_cb)) {}

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
  AcceptCallback accept_cb_;

  std::string output_;

  // When set, the accept callback will automatically add the new line to history.
  bool accept_goes_to_history_ = false;

  // The parameter from the most recent "accept" call, or none if not called.
  std::optional<std::string> accept_;
};

}  // namespace line_input

#endif  // SRC_LIB_LINE_INPUT_TEST_LINE_INPUT_H_
