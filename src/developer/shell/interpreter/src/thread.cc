// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>
#include <string>

#include "src/developer/shell/interpreter/src/code.h"
#include "src/developer/shell/interpreter/src/interpreter.h"
#include "src/lib/fxl/logging.h"

namespace shell {
namespace interpreter {

void Thread::Execute(ExecutionContext* context, std::unique_ptr<code::Code> code) {
  ExecutionScope scope;
  scope.Execute(context, this, std::move(code));
  context->interpreter()->ContextDone(context);
  FXL_DCHECK(values_.empty());
}

}  // namespace interpreter
}  // namespace shell
