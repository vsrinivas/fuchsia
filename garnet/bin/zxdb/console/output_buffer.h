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

const char* SyntaxToString(Syntax);

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

const char* TextBackgroundColorToString(TextBackgroundColor);

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

const char* TextForegroundColorToString(TextBackgroundColor);

// This class collects output from commands so it can be put on the screen in
// one chunk. It's not just a string because we want to add helper functions
// and may want to add things like coloring in the future.
class OutputBuffer {
 public:
  OutputBuffer();

  // Creates an output buffer with one substring in it.
  OutputBuffer(std::string str,
               TextForegroundColor fg = TextForegroundColor::kDefault,
               TextBackgroundColor bg = TextBackgroundColor::kDefault);
  OutputBuffer(Syntax syntax, std::string str);

  ~OutputBuffer();

  // Appends the given type.
  void Append(std::string str,
              TextForegroundColor fg = TextForegroundColor::kDefault,
              TextBackgroundColor bg = TextBackgroundColor::kDefault);
  void Append(Syntax syntax, std::string str);
  void Append(OutputBuffer buffer);
  void Append(const Err& err);

  // Outputs the given help string, applying help-style formatting.
  void FormatHelp(const std::string& str);

  // Writes the current contents of this OutputBuffer to stdout.
  void WriteToStdout() const;

  // Concatenates to a single string with no formatting.
  std::string AsString() const;

  // Returns the number of Unicode characters in the buffer. Backed by the
  // version in string_util.h, see that for documentation.
  size_t UnicodeCharWidth() const;

  void Clear();

  bool empty() const { return spans_.empty(); }

  // Formats this buffer's formatting into a text form for testing. It will be
  // normalized (so adjacent spans of the same format will be combined).
  //
  // The format is
  //   <format> "<text>", <format> "<text>", ...
  // The format is the syntax enum name, and if either foreground or background
  // are non-default, background and foreground, they will both follow (always
  // together), separated by spaces. So:
  //   kComment "foo", kNormal kGreen kGray "bar"
  std::string GetDebugString() const;

 private:
  struct Span {
    Span(Syntax s, std::string t);
    Span(std::string t, TextForegroundColor fg, TextBackgroundColor bg);

    Syntax syntax = Syntax::kNormal;

    // Explicit colors will only be used when Syntax is kNormal.
    TextForegroundColor foreground = TextForegroundColor::kDefault;
    TextBackgroundColor background = TextBackgroundColor::kDefault;

    std::string text;
  };
  std::vector<Span> spans_;
};

}  // namespace zxdb
