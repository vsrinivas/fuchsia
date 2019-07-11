// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_

#include <functional>

#include "lib/fit/defer.h"
#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

class EvalContext;
struct FormatOptions;
class FormatNode;

// Fills the value() of a FormatNode based on its expression. This does not
// update the description based on the new value. The node can be in any state
// and this function will fill the value if possible.
//
// The callback will be called on completion. This may occur synchronously
// (within the stack of this function call) or in the future. If it happens in
// the future, the node will be referenced by weak pointer so the caller does
// not have to worry about lifetime issues.
//
// The callback will always be issued, even if the node is destroyed. Callers
// should keep a weak pointer to the node if they do not control its lifetime.
//
// TODO(brettw) should this be a member of FormatNode?
void FillFormatNodeValue(FormatNode* node, fxl::RefPtr<EvalContext> context,
                         fit::deferred_callback cb);

// Fills the description and children of a FormatNode based on the current
// value().
//
// The callback will be called on completion. This may occur synchronously
// (within the stack of this function call) or in the future. If it happens in
// the future, the node will be referenced by weak pointer so the caller does
// not have to worry about lifetime issues.
//
// The callback will always be issued, even if the node is destroyed. Callers
// should keep a weak pointer to the node if they do not control its lifetime.
void FillFormatNodeDescription(FormatNode* node, const FormatOptions& options,
                               fxl::RefPtr<EvalContext> context, fit::deferred_callback cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_H_
