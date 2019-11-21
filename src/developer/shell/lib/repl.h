// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_LIB_REPL_H_
#define SRC_DEVELOPER_SHELL_LIB_REPL_H_

#include <string>

#include "src/lib/line_input/line_input.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

namespace shell::repl {

// This class implements a Javascript repl, where scripts are executed in the JSContext (created
// with //thirdparty/quickjs) passed to the constructor.
// Once created it can be fed input through FeedInput, until FeedInput returns true (when a \q
// command is detected in the input): the repl will not accept any more input, and all input after
// \q was ignored.
// Two shell specific commands are available: \h for help and \q to exit
class Repl {
 public:
  Repl(JSContext* ctx, const std::string& prompt);
  // Takes as argument a buffer containing the characters of input, and the number of characters
  // Returns true if "\q" was entered at the beginning of a line
  bool FeedInput(uint8_t* bytes, size_t num_bytes);
  virtual ~Repl() = default;

 protected:
  // used for testing, allows acces to the //src/lib/line_input editor
  Repl(JSContext* ctx, line_input::LineInputStdout li);
  // returns true if "\q" (exiting the shell) was typed
  virtual void HandleLine(const std::string& line);
  virtual const char* EvalCmd(std::string& cmd);
  // Given a possibly incomplete Javascript script, returns the list of currently open brackets ({[,
  // * for block comments and / for regular expressions
  std::string OpenSymbols(std::string& cmd);
  // Output function used to print the result of the Javascript script
  void Write(const char* output);

 private:
  std::string GetAndExecuteShellCmd(std::string cmd);
  std::string mexpr_;
  line_input::LineInputStdout li_;
  JSContext* ctx_;
  FILE* output_fd_;
  bool exit_shell_cmd_;
};
}  // namespace shell::repl

#endif  // SRC_DEVELOPER_SHELL_LIB_REPL_H_
