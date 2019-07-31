// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_STD_STRING_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_STD_STRING_H_

#include "src/developer/debug/zxdb/expr/pretty_type.h"

namespace zxdb {

// C++ std::string.
class PrettyStdString : public PrettyType {
 public:
  PrettyStdString() = default;

  // PrettyType implementation.
  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
  EvalFunction GetGetter(const std::string& getter_name) const override;
  EvalArrayFunction GetArrayAccess() const override;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_STD_STRING_H_
