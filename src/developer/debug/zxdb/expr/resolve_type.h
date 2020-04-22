// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_TYPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_TYPE_H_

#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

struct FindNameContext;
class Type;

// Strips C-V qualifications and resolves forward declarations.
//
// This is the function to use to properly resolve the type to something there the data of the
// ExprValue can be interpreted.
//
// It will return null only if the input type is null. Sometimes forward declarations can't be
// resolved or the "const" refers to nothing, in which case this function will return the original
// type.
fxl::RefPtr<Type> GetConcreteType(const FindNameContext& context, const Type* type);

// Looks for a type definition matching the name of the input type. If none exists, returns the
// input type. The only time this will return null is if the input is null. This will search for an
// exact match on the name. Most code will want to use GetConcreteType() above which strips C-V
// qualifications.
//
// This is used to ensure that the type is not a foward-declaration (if possible).
fxl::RefPtr<Type> FindTypeDefinition(const FindNameContext& context, const Type* type);

// Looks for a type definition matching the given fully-qualified name. Returns null if not found.
fxl::RefPtr<Type> FindTypeDefinition(const FindNameContext& context, ParsedIdentifier looking_for);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_TYPE_H_
