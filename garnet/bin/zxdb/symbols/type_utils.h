// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

class Collection;
class Type;

// Verifies that |type| is a pointer and fills the pointed-to type into
// |*pointed_to|. In other cases, returns an error. The input type can be null
// (which will produce an error) so the caller doesn't have to check.
Err GetPointedToType(const Type* input, const Type** pointed_to);

// Tries to interpret the type as a pointed to a Collection. On success,
// places the output into |*coll|.
Err GetPointedToCollection(const Type* type, const Collection** coll);

}  // namespace zxdb
