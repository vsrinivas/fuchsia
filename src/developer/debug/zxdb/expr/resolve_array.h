// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_ARRAY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_ARRAY_H_

#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class Type;
class EvalContext;

// Converts the given array type (could be a pointer or a static array type like "int[4]") to
// a vector of ExprValues. Since this requires memory fetches is must be asynchronous.
//
// The input will be clipped to the array size so the result may be empty or smaller than requested.
//
// This does not apply pretty types for item resolution.
void ResolveArray(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& array,
                  size_t begin_index, size_t end_index, fit::callback<void(ErrOrValueVector)>);

// Resolves a single item in an array and applies pretty types for item resolution. This is the
// backend for array access [ <number> ] in expressions.
void ResolveArrayItem(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& array,
                      size_t index, EvalCallback cb);

// Forces an array to one of a different size.
//
// Converts a pointer to a static array of the given size by fetching the corresponding memory.
//
// Converts a static array's type to represent the new size. For example, resizing an array of
// type "double[16]" to length 8 will copy the data and the new type will be "double[8]". To
// support expanding the length of a static array, the memory will be fetched according to the
// source of the static array (if there is no memory as the source of the array it will fail).
void CoerceArraySize(const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& array,
                     size_t new_size, EvalCallback cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_ARRAY_H_
