// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/console/console_context.h"
#include "garnet/bin/zxdb/console/line_input.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class OutputBuffer;
class Session;

class Console {
 public:
  // The result of dispatching input is either to keep running or quit the
  // message loop to exit.
  enum class Result { kContinue, kQuit };

  explicit Console(Session* session);
  ~Console();

  static Console* get() { return singleton_; }

  ConsoleContext& context() { return context_; }

  // Prints the first prompt to the screen. This only needs to be called once.
  void Init();

  // Returns what should happen as a result of this input.
  Result OnInput(char c);

  // Prints the buffer/string to the console.
  void Output(OutputBuffer output);
  void Output(const std::string& s);
  void Output(const Err& err);

 private:
  Result DispatchInputLine(const std::string& line);

  static Console* singleton_;

  ConsoleContext context_;

  LineInputStdout line_input_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Console);
};

}  // namespace zxdb
