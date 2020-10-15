// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/word-view.h>

using namespace std::literals;

WordView::iterator& WordView::iterator::operator++() {
  Check();

  // Advance past the current word.
  rest_.remove_prefix(word_.size());

  constexpr auto kWhitespace = " \n\r\t\0"sv;
  if (auto nonws = rest_.find_first_not_of(kWhitespace); nonws == std::string_view::npos) {
    // Nothing but whitespace, so no more words.  Advance to end() state.
    rest_.remove_prefix(rest_.size());
    // operator*() is invalid in end() state, but maintain Check() invariant.
    word_ = rest_;
  } else {
    rest_.remove_prefix(nonws);
    if (auto ws = rest_.find_first_of(kWhitespace); ws == std::string_view::npos) {
      // No more whitespace, so this is the last word.
      word_ = rest_;
    } else {
      // Chop off the word operator* returns.  Next ++ will advance past it.
      word_ = rest_.substr(0, ws);
    }
  }

  Check();
  return *this;
}
