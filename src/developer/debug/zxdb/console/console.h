// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_H_

#include <map>

#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/line_input/modal_line_input.h"

namespace zxdb {

class AsyncOutputBuffer;
class OutputBuffer;
class Session;

class Console {
 public:
  explicit Console(Session* session);
  virtual ~Console();

  static Console* get() { return singleton_; }

  ConsoleContext& context() { return context_; }

  fxl::WeakPtr<Console> GetWeakPtr();

  // Prints the first prompt to the screen. This only needs to be called once.
  virtual void Init(){};

  // Prints the buffer/string to the console.
  virtual void Output(const OutputBuffer& output) = 0;
  void Output(const std::string& s);
  void Output(const Err& err);

  // Synchronously prints the output if the async buffer is complete. Otherwise adds a listener and
  // prints the output to the console when it is complete.
  void Output(fxl::RefPtr<AsyncOutputBuffer> output);

  // Clears the contents of the console.
  virtual void Clear() = 0;

  // Asks the user a question. The possible answers are stored in the options struct.
  //
  // Callers should pass anything they want to print above the prompt in the |message|. It's
  // important to do this instead of calling Output() followed by ModalGetOption() because there
  // can theoretically be multiple prompts pendingx (in case they're triggered by async events)
  // and the message passed here will always get printed above the prompt when its turn comes.
  virtual void ModalGetOption(const line_input::ModalPromptOptions& options, OutputBuffer message,
                              const std::string& prompt,
                              line_input::ModalLineInput::ModalCompletionCallback cb) = 0;

  // The result of dispatching input is either to keep running or quit the message loop to exit.
  enum class Result { kContinue, kQuit };

  // DispatchInputLine will generate the result by parsing the command. Depending on this result,
  // this function could stop the MessageLoop. We pass the result out for callers to use and react
  // accordingly, which can indicate whether they want the console to continue processing commands.
  virtual Result ProcessInputLine(const std::string& line, CommandCallback callback = nullptr) = 0;

 protected:
  static Console* singleton_;
  ConsoleContext context_;

 private:
  // Track all asynchronous output pending. We want to store a reference and lookup by pointer, so
  // the object is duplicated here (RefPtr doesn't like to be put in a set).
  std::map<AsyncOutputBuffer*, fxl::RefPtr<AsyncOutputBuffer>> async_output_;

  fxl::WeakPtrFactory<Console> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Console);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_CONSOLE_H_
