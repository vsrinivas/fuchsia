// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/function_return_info.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

void FunctionReturnInfo::InitFromTopOfStack(Thread* t) {
  const Stack& stack = t->GetStack();
  if (stack.empty())
    return;

  thread = t;
  symbol = stack[0]->GetLocation().symbol();
}

}  // namespace zxdb
