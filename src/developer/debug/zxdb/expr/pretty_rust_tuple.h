// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_RUST_TUPLE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_RUST_TUPLE_H_

#include "src/developer/debug/zxdb/expr/pretty_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"

namespace zxdb {

// Rust tuple
class PrettyRustTuple : public PrettyType {
 public:
  // PrettyType implementation.
  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;
  EvalFunction GetMember(const std::string& getter_name) const override;
};

// Rust tuple
class PrettyRustZirconStatus : public PrettyRustTuple {
 public:
  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_RUST_TUPLE_H_
