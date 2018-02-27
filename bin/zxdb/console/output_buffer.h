// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>
#include <string>

namespace zxdb {

class Err;

// This class collects output from commands so it can be put on the screen in
// one chunk. It's not just a string because we want to add helper functions
// and may want to add things like coloring in the future.
class OutputBuffer {
 public:
  OutputBuffer();
  ~OutputBuffer();

  // Appends a string.
  void Append(const std::string& str);

  // Outputs the given help string, applying help-style formatting.
  void FormatHelp(const std::string& str);

  // Writes the given error.
  void OutputErr(const Err& err);

  // Writes the current contents of this OutputBuffer to stdout.
  void WriteToStdout();

 private:
  std::string str_;
};

}  // namespace zxdb
