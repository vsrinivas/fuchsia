// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_H_

#include "lib/fit/defer.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"

namespace zxdb {

class FormatNode;
struct FormatOptions;

class PrettyType {
 public:
  PrettyType() = default;

  // Fills the given FormatNode. Upon completion, issues the given deferred_callback. If the format
  // node is filled asynchronously the implementation should take a weak pointer to it since the
  // lifetime is not guaranteed.
  virtual void Format(FormatNode* node, const FormatOptions& options,
                      fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_TYPE_H_
