// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_

#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

class EvalContext;
struct FormatExprValueOptions;
class FormatNode;

// Fills the value() of a FormatNode based on its expression. This does not
// update the description based on the new value.
//
// This may occur synchronously or in the future. If it happens in the future,
// the node will be referenced by weak pointer so the caller does not have to
// worry about lifetime issues.
void FillFormatNodeValue(FormatNode* node, fxl::RefPtr<EvalContext> context);

// Fills the description and children of a FormatNode based on the current
// value().
void FillFormatNodeDescription(FormatNode* node,
                               const FormatExprValueOptions& options,
                               fxl::RefPtr<EvalContext> context);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_
