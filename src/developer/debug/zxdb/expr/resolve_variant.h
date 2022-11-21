// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_VARIANT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_VARIANT_H_

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Collection;
class EvalContext;
class Variant;
class VariantPart;

// Given the VariantPart stored in the given ExprValue, this computes the currently active Variant
// inside the given collection and places it into *result.
Err ResolveVariant(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                   const Collection* collection, const VariantPart* variant_part,
                   fxl::RefPtr<Variant>* result);

// Returns the short name of the active Rust enum value (for example, this will be "Some" or "None"
// for an Option). If the struct doesn't look like a Rust enum, returns an error.
ErrOr<std::string> GetActiveRustVariantName(const fxl::RefPtr<EvalContext>& context,
                                            const ExprValue& value);

// Extracts the first variant value in the given collection. Practically, this means it returns
// the current active data from a Rust enum.
//
// DWARF supports multiple variant values but the only case we have for this is Rust enums which
// only have a single value (it will be a tuple or a struct if the user wants more than one thing
// stored in the enum). This function will fail if there is more than one data member in the
// variant.
ErrOrValue ResolveSingleVariantValue(const fxl::RefPtr<EvalContext>& context,
                                     const ExprValue& value);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_VARIANT_H_
