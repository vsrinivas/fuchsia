// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_ISOLATE_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_ISOLATE_H_

#include <vector>

#include "src/developer/shell/interpreter/src/scope.h"

namespace shell {
namespace interpreter {

// Defines an isolate. Each isolate is completely independent from the others.
class Isolate {
 public:
  Isolate() = default;

  Scope* global_scope() { return &global_scope_; }

 private:
  // Global scope for the isolate. Holds the global variables.
  Scope global_scope_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_ISOLATE_H_
