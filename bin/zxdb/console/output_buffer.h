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
enum class Syntax { kNormal, kComment, kHeading, kError, kWarning, kSpecial };

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

 private:
  struct Span {
    Span(Syntax s, std::string t);

    Syntax syntax = Syntax::kNormal;
    std::string text;
  };
  std::vector<Span> spans_;
};

}  // namespace zxdb
