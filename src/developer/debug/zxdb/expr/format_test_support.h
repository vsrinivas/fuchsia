// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_TEST_SUPPORT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_TEST_SUPPORT_H_

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"

namespace zxdb {

// Evaluates and describes a single given node synchronously, running the message loop if necessary.
// This is not recursive.
void SyncFillAndDescribeFormatNode(const fxl::RefPtr<EvalContext>& eval_context, FormatNode* node,
                                   const FormatOptions& opts);

// A recursive version of SyncFillAndDescribeFormatNode(), this iterates into all children until
// there are no more children.
//
// Running this on real data can result in infinite recursion if there is a pointer loop.
void SyncFillAndDescribeFormatTree(const fxl::RefPtr<EvalContext>& eval_context, FormatNode* node,
                                   const FormatOptions& opts);

// Returns either:
//   <type>, <description>
// if it's valid, or:
//   Err: <err_message>
// if there's an error.
std::string GetFormatNodeTypeAndDescription(const FormatNode* node);

// Fills the node's contents into a text structure, with each level indented two spaces. This does
// not fill node values or describe the nodes.
//
// <name> = <type>, <description>
//   <child name> = <child type>, <child description>
//     <child level 2 name> = <child 2 type>, <child 2 description>
//   <child name> = <child type>, <child description>
std::string GetDebugTreeForFormatNode(const FormatNode* node);

// Formats and describes the given ExprValue according to GetDebugTreeForExprValue() above.
//
// Note that normally the root name will be empty so it will start with " = <type>, <description>"
std::string GetDebugTreeForValue(const fxl::RefPtr<EvalContext>& eval_context,
                                 const ExprValue& value, const FormatOptions& opts);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_TEST_SUPPORT_H_
