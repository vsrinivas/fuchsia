// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_STRING_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_STRING_H_

#include "src/developer/debug/zxdb/expr/pretty_type.h"

namespace zxdb {

// C++ std::string.
class PrettyStdString : public PrettyType {
 public:
  PrettyStdString() = default;

  // PrettyType implementation.
  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
};

// C++ std::string_view
class PrettyStdStringView : public PrettyType {
 public:
  PrettyStdStringView() = default;

  // PrettyType implementation.
  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
};

// Rust &str.
class PrettyRustStr : public PrettyType {
 public:
  PrettyRustStr() = default;

  // PrettyType implementation.
  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
};

// Rust string::String.
class PrettyRustString : public PrettyType {
 public:
  PrettyRustString() = default;

  // PrettyType implementation.
  void Format(FormatNode* node, const FormatOptions& options, fxl::RefPtr<EvalContext> context,
              fit::deferred_callback cb) override;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_PRETTY_STRING_H_
