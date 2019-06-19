// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_

#include <functional>

#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

class EvalContext;
struct FormatExprValueOptions;
class FormatNode;

// Fills the value() of a FormatNode based on its expression. This does not
// update the description based on the new value. The node can be in any state
// and this function will fill the value if possible.
//
// The callback will be called on completion. This may occur synchronously
// (within the stack of this function call) or in the future. If it happens in
// the future, the node will be referenced by weak pointer so the caller does
// not have to worry about lifetime issues. The callback will not be issued if
// the node is destroyed.
//
// TODO(brettw) should this be a member of FormatNode?
void FillFormatNodeValue(FormatNode* node, fxl::RefPtr<EvalContext> context,
                         std::function<void()> cb);

// Fills the description and children of a FormatNode based on the current
// value().
//
// This is currently synchronous. In the future the one-line description of
// a node might want to include information from the children that describing
// adds (for example, describing might add a pointer-dereference child node,
// and we would want to include that value in the description of this one).
// If that's needed we'll need to add a callback here.
void FillFormatNodeDescription(FormatNode* node,
                               const FormatExprValueOptions& options,
                               fxl::RefPtr<EvalContext> context);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_
