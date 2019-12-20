// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_PTR_REF_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_PTR_REF_H_

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class EvalContext;
class Type;

// Creates an ExprValue of the given type from the data at the given address. Issues the callback on
// completion. The type can be null (it will immediately call the callback with an error).
//
// It's assumed the type is already concrete (so it has a size). This will not do any fancy stuff
// like casting to a derived type. It is a low-level function that just fetches the requested
// memory.
void ResolvePointer(const fxl::RefPtr<EvalContext>& eval_context, uint64_t address,
                    fxl::RefPtr<Type> type, EvalCallback cb);

// Similar to the above but the pointer and type comes from the given ExprValue, which is assumed to
// be a pointer type. If it's not a pointer type, the callback will be issued with an error.
//
// This will automatically cast to a derived type if the EvalContext requests it, so the resulting
// object may be a different type or from a different address than the input pointer value.
void ResolvePointer(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& pointer,
                    EvalCallback cb);

// Ensures that the value is not a reference type (rvalue or regular). The result will be an
// ExprValue passed to the callback that has any reference types stripped.
//
// If the input ExprValue does not have a reference type, calls the callback immediately (from
// within the calling function's stack frame) with the input.
//
// If the value is a reference type, it will be resolved and the value will be the value of the
// referenced data.
void EnsureResolveReference(const fxl::RefPtr<EvalContext>& eval_context, ExprValue value,
                            EvalCallback cb);

// Verifies that |input| type is a pointer and fills the pointed-to type into |*pointed_to|. In
// other cases, returns an error. The input type can be null (which will produce an error) or
// non-concrete (const, forward definition, etc.) so the caller doesn't have to check.
//
// The returned type may not necessarily be concrete (need to preserve, const, etc.).
Err GetPointedToType(const fxl::RefPtr<EvalContext>& eval_context, const Type* input,
                     fxl::RefPtr<Type>* pointed_to);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_PTR_REF_H_
