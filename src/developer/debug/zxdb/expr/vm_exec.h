// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_EXEC_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_EXEC_H_

#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/vm_op.h"
#include "src/developer/debug/zxdb/expr/vm_stream.h"

namespace zxdb {

void VmExec(const fxl::RefPtr<EvalContext>& eval_context, VmStream stream, EvalCallback cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VM_EXEC_H_
