// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_REPL_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_REPL_H_

#include <string>

#include "src/lib/line_input/line_input.h"
#include "third_party/quickjs/quickjs-libc.h"
#include "third_party/quickjs/quickjs.h"

namespace shell::repl {

// This class implements a Javascript repl.
// Once created it can be fed input through FeedInput, until FeedInput returns true (when a \q
// command is detected in the input): the repl will not accept any more input, and all input after
// \q was ignored.
// Before execution, the prompt is hidden and the scripts are wrapped in the
// evalScriptAwaitsPromise() (repl_cc.js) function: this function executes the script, then:
// - if its result is a promise: it waits (through a callback function to the promise) for it to
// resolve (or reject), then prints its result and shows the prompt again
// - else it prints it and shows the prompt immediately

// Three shell specific commands are available: \h for help, \q to exit, c to make the prompt show
// again after an uncaught error in a promise

class Repl {
 public:
  Repl(JSContext* ctx, const std::string& prompt);
  // Takes as argument a buffer containing the characters of input, and the number of characters
  // Ignores all input but 'Ctrl-Z' if a command is still running (ie running_ is set to true)
  // Returns true if "\q" was entered at the beginning of a line
  bool FeedInput(uint8_t* bytes, size_t num_bytes);
  // Returns the cmd stored in cur_cmd_ field
  const char* GetCmd();
  // Returns the line stored in cur_line_ field
  const char* GetLine();
  // Shows the prompt, and sets running_ to false
  void ShowPrompt();
  void Write(const char* output);
  void ChangeOutput(std::ostream* os);
  virtual ~Repl() = default;

 protected:
  // used for testing
  Repl(JSContext* ctx, const std::string& prompt, fit::function<void(const std::string&)> cb);
  // sets exit_shell_cmd_ to true if "\q" (exiting the shell) was typed
  virtual void HandleLine(const std::string& line);
  // calls the JS function evalScriptAwaitsPromise()
  virtual void EvalCmd(const std::string& cmd);
  // Given a possibly incomplete Javascript script, returns the list of currently open brackets ({[,
  // * for block comments and / for regular expressions
  std::string OpenSymbols(const std::string& cmd);

 private:
  std::string GetAndExecuteShellCmd(std::string cmd);
  std::string mexpr_;
  line_input::LineInputStdout li_;
  JSContext* ctx_;
  std::ostream* output_;
  bool exit_shell_cmd_;
  bool running_;  // set to true at the beginning of a JS script execution, and set to false by
                  // ShowPrompt().
  std::string cur_cmd_;
  std::string line_to_complete_;
};
}  // namespace shell::repl

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_REPL_H_
