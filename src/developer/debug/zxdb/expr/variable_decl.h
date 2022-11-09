// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VARIABLE_DECL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VARIABLE_DECL_H_

#include <string>

#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_language.h"
#include "src/developer/debug/zxdb/expr/vm_stream.h"

// This file contains the backend implementation for VariableDeclExprNode.

namespace zxdb {

class ExprNode;

// Stores the information for a variable declaration. Only some specific forms of "auto" are
// supported, see the .cc file for an overview.
struct VariableDeclTypeInfo {
  std::string ToString() const;

  enum Kind {
    kCAuto,     // auto
    kCAutoRef,  // auto&
    kCAutoPtr,  // auto*
    kRustAuto,  // Implicit type in a let statement.
    kExplicit,  // Explicitly-given type name.
  };

  Kind kind = kCAuto;

  // When the type is kExplicit, this is the type requested.
  fxl::RefPtr<Type> concrete_type;
};

// Decodes any auto type specifiers for the variable declaration of the given type.
ErrOr<VariableDeclTypeInfo> GetVariableDeclTypeInfo(ExprLanguage lang,
                                                    fxl::RefPtr<Type> concrete_type);

// Emits bytecode to the given stream to handle the following constructs:
//
//   int i;                  (C, null init_expr)
//   int i = 5 * something;  (C, init_expr)
//   let i: i32;             (Rust, explicit type with no init_expr).
//   let i = 99;             (Rust, init_expr with null type).
//
// Since this function does not take an EvalContext the input type must be concrete if it is
// supplied. It may be null to indicate "auto" (takes the type from the init expression).
void EmitVariableInitializerOps(const VariableDeclTypeInfo& decl_info, uint32_t local_slot,
                                fxl::RefPtr<ExprNode> init_expr, VmStream& stream);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VARIABLE_DECL_H_
