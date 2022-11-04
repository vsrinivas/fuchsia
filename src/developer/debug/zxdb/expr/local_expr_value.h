// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_LOCAL_EXPR_VALUE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_LOCAL_EXPR_VALUE_H_

#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

// This is a reference counted ExprValue. It is used to store "local" variables (ones that the
// debugger script has created and exist only in the local debugger, not in the debugged program).
//
// These need to be reference counted because they're referred to by the ExprValueSource and those
// can get copied around. This relationship substitutes for the "pointer" property of real data and
// is how updates happen to these values.
class LocalExprValue : public fxl::RefCountedThreadSafe<LocalExprValue> {
 public:
  ExprValue GetValue() const {
    // Make the returned value reference ourselves as its "source".
    return ExprValue(value_.type_ref(), value_.data(), ExprValueSource(RefPtrTo(this)));
  }
  void SetValue(const ExprValue& v) {
    // Ensure that the "source" is unset to prevent accidental ref cycles.
    value_ = ExprValue(v.type_ref(), v.data());
  }

 private:
  FRIEND_MAKE_REF_COUNTED(LocalExprValue);
  FRIEND_REF_COUNTED_THREAD_SAFE(LocalExprValue);

  explicit LocalExprValue(ExprValue v) : value_(std::move(v)) {}

  // This can not have its "source" set since that will point back to this LocalExprValue and
  // create a reference cycle. It is set when an ExprValue is returned by copy.
  ExprValue value_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_LOCAL_EXPR_VALUE_H_
