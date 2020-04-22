// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

struct FindNameContext;
class Type;

// Looks for a type definition matching the name of the input type. If none exists, returns the
// input type. The only time this will return null is if the input is null.
//
// This is used to ensure that the type is not a foward-declaration (if possible).
fxl::RefPtr<Type> FindTypeDefinition(const FindNameContext& context, const Type* type);

// Looks for a type definition matching the given fully-qualified name. Returns null if not found.
fxl::RefPtr<Type> FindTypeDefinition(const FindNameContext& context, ParsedIdentifier looking_for);

}  // namespace zxdb
