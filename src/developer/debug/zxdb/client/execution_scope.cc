// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/execution_scope.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

ExecutionScope::ExecutionScope(Target* t) : type_(kTarget), target_(t->GetWeakPtr()) {}

ExecutionScope::ExecutionScope(Thread* t)
    : type_(kThread),
      target_(t->GetProcess()->GetTarget()->GetWeakPtr()),
      thread_(t->GetWeakPtr()) {}

}  // namespace zxdb
