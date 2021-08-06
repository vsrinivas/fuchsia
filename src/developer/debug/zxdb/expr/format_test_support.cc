// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/format_test_support.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/expr/format.h"

namespace zxdb {

namespace {

void AppendDebugTreeForNode(const FormatNode* node, int indent, std::string* output) {
  output->append(std::string(indent * 2, ' '));
  output->append(node->name());
  output->append(" = ");
  output->append(GetFormatNodeTypeAndDescription(node));
  output->append("\n");
  for (auto& child : node->children())
    AppendDebugTreeForNode(child.get(), indent + 1, output);
}

}  // namespace

void SyncFillAndDescribeFormatNode(const fxl::RefPtr<EvalContext>& eval_context, FormatNode* node,
                                   const FormatOptions& opts) {
  // Populate the value.
  bool called = false;
  FillFormatNodeValue(node, eval_context, fit::defer_callback([&called]() { called = true; }));
  debug::MessageLoop::Current()->RunUntilNoTasks();
  FX_DCHECK(called);

  called = false;
  FillFormatNodeDescription(node, opts, eval_context, fit::defer_callback([&called]() {
                              debug::MessageLoop::Current()->QuitNow();
                              called = true;
                            }));
  debug::MessageLoop::Current()->RunUntilNoTasks();
  FX_DCHECK(called);
}

void SyncFillAndDescribeFormatTree(const fxl::RefPtr<EvalContext>& eval_context, FormatNode* node,
                                   const FormatOptions& opts) {
  SyncFillAndDescribeFormatNode(eval_context, node, opts);
  for (auto& child : node->children())
    SyncFillAndDescribeFormatTree(eval_context, child.get(), opts);
}

std::string GetFormatNodeTypeAndDescription(const FormatNode* node) {
  if (node->err().has_error())
    return "Err: " + node->err().msg();
  return node->type() + ", " + node->description();
}

std::string GetDebugTreeForFormatNode(const FormatNode* node) {
  std::string result;
  AppendDebugTreeForNode(node, 0, &result);
  return result;
}

std::string GetDebugTreeForValue(const fxl::RefPtr<EvalContext>& eval_context,
                                 const ExprValue& value, const FormatOptions& opts) {
  auto node = std::make_unique<FormatNode>(std::string(), value);
  SyncFillAndDescribeFormatTree(eval_context, node.get(), opts);

  return GetDebugTreeForFormatNode(node.get());
}

}  // namespace zxdb
