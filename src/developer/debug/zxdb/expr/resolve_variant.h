// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_VARIANT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_VARIANT_H_

#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Collection;
class EvalContext;
class ExprValue;
class Variant;
class VariantPart;

// Given the VariantPart stored in the given ExprValue, this computes the currently active Variant
// inside the given collection and places it into *result.
Err ResolveVariant(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                   const Collection* collection, const VariantPart* variant_part,
                   fxl::RefPtr<Variant>* result);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_VARIANT_H_
