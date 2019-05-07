// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

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

  // Clears the contents of the console.
  virtual void Clear() = 0;

  // The result of dispatching input is either to keep running or quit the
  // message loop to exit.
  enum class Result { kContinue, kQuit };

  // DispatchInputLine will generate the result by parsing the command.
  // Depending on this result, this function could stop the MessageLoop.
  // We pass the result out for callers to use and react accordingly, which
  // can indicate whether they want the console to continue processing
  // commands.
  virtual Result ProcessInputLine(const std::string& line,
                                  CommandCallback callback = nullptr) = 0;

 protected:
  static Console* singleton_;

  ConsoleContext context_;

  fxl::WeakPtrFactory<Console> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Console);
};

}  // namespace zxdb
