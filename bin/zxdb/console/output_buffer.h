// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>
#include <string>
#include <vector>

namespace zxdb {

class Err;

// "Special" is used to note something unusual or weird.
enum class Syntax {
  kNormal,
  kComment,
  kHeading,
  kError,
  kWarning,
  kSpecial,
  kReversed,
  kVariable  // Use for variable names.
};

// The following color enums are to be used when Syntax is not enough, which
// is meant to semantic meaning. Colors are to be used by specific output that
// use more fine-grained control over color output, like the register output
// table.
// Colors never override syntax. They are only applied when the Span is using
// a normal syntax.

enum class TextBackgroundColor {
  kDefault,
  // Basic 16 colors
  kBlack,
  kBlue,
  kCyan,
  kGray,
  kGreen,
  kMagenta,
  kRed,
  kYellow,
  kWhite,

  kLightBlue,
  kLightCyan,
  kLightGray,
  kLightGreen,
  kLightMagenta,
  kLightRed,
  kLightYellow,
};

enum class TextForegroundColor {
  kDefault,
  // Basic 16 colors
  kBlack,
  kBlue,
  kCyan,
  kGray,
  kGreen,
  kMagenta,
  kRed,
  kYellow,
  kWhite,

  kLightBlue,
  kLightCyan,
  kLightGray,
  kLightGreen,
  kLightMagenta,
  kLightRed,
  kLightYellow,
};

// This class collects output from commands so it can be put on the screen in
// one chunk. It's not just a string because we want to add helper functions
// and may want to add things like coloring in the future.
class OutputBuffer {
 public:
  OutputBuffer();
  ~OutputBuffer();

  // Helpers to construct an OutputBuffer with one substring in it.
  static OutputBuffer WithContents(std::string str);
  static OutputBuffer WithContents(Syntax syntax, std::string str);

  // Appends a string or another OutputBuffer.
  void Append(std::string str);
  void Append(Syntax syntax, std::string str);
  void Append(OutputBuffer buffer);

  // Outputs the given help string, applying help-style formatting.
  void FormatHelp(const std::string& str);

  // Writes the given error.
  void OutputErr(const Err& err);

  // Writes the current contents of this OutputBuffer to stdout.
  void WriteToStdout() const;

  // Concatenates to a single string with no formatting.
  std::string AsString() const;

  // Returns the number of Unicode characters in the buffer. Backed by the
  // version in string_util.h, see that for documentation.
  size_t UnicodeCharWidth() const;

  void SetBackgroundColor(TextBackgroundColor);
  void SetForegroundColor(TextForegroundColor);

 private:
  struct Span {
    Span(Syntax s, std::string t);

    Syntax syntax = Syntax::kNormal;
    // This will only be used when Syntax is kNormal.
    // This is normally set through the OutputBuffer interface.
    TextBackgroundColor background = TextBackgroundColor::kDefault;
    TextForegroundColor foreground = TextForegroundColor::kDefault;
    std::string text;
  };
  std::vector<Span> spans_;
};

}  // namespace zxdb
