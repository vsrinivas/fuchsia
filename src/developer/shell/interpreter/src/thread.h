// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_THREAD_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_THREAD_H_

#include <deque>
#include <vector>

#include "src/developer/shell/interpreter/src/code.h"
#include "src/developer/shell/interpreter/src/scope.h"
#include "src/lib/syslog/cpp/logger.h"

namespace shell {
namespace interpreter {

class Isolate;

// Defines a thread. A thread can execute some code in parallel with other threads. Each one has
// its own value stack (equivalent of registers) and it's own program counter.
class Thread {
 public:
  explicit Thread(Isolate* isolate) : isolate_(isolate) {}

  Isolate* isolate() const { return isolate_; }

  // Pops one 64 bit value from the value stack.
  uint64_t Pop() {
    FX_DCHECK(!values_.empty());
    uint64_t returned_value = values_.back();
    values_.pop_back();
    return returned_value;
  }

  // Pushes one 64 bit value to the value stack.
  void Push(uint64_t value) { values_.push_back(value); }

  // Executes |code| for |context| using this thread.
  void Execute(ExecutionContext* context, std::unique_ptr<code::Code> code);

 private:
  // The isolate associated to the threda.
  Isolate* const isolate_;
  // The value stack (used to compute expressions).
  std::deque<uint64_t> values_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_THREAD_H_
