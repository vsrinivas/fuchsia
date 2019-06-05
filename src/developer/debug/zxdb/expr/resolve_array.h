// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_ARRAY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_ARRAY_H_

#include <functional>
#include <vector>

#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class ExprValue;
class Type;
class EvalContext;

// Gets the values from a range given an array of a given type. The end index
// is the index of one-past-the-end of the desired data.
//
// The input will be clipped to the array size so the result may be empty
// or smaller than requested.
//
// This variant works only for static array types ("foo[5]") where the size is
// known constant at compile time and therefor the entire array is contained
// in the ExprValue's data.
Err ResolveArray(fxl::RefPtr<EvalContext> eval_context, const ExprValue& array,
                 size_t begin_index, size_t end_index,
                 std::vector<ExprValue>* result);

// This variant handles both the static array version above and also
// dereferencing pointers using array indexing. Since this requires memory
// fetches is must be asynchronous.
//
// The input will be clipped to the array size so the result may be empty
// or smaller than requested.
void ResolveArray(fxl::RefPtr<EvalContext> eval_context, const ExprValue& array,
                  size_t begin_index, size_t end_index,
                  std::function<void(const Err&, std::vector<ExprValue>)> cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_ARRAY_H_
