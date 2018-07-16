// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/modified_type.h"

namespace zxdb {

// static
bool ModifiedType::IsTypeModifierTag(int tag) {
  return tag == kTagConstType || tag == kTagPointerType ||
         tag == kTagReferenceType || tag == kTagRestrictType ||
         tag == kTagRvalueReferenceType || tag == kTagTypedef ||
         tag == kTagVolatileType || tag == kTagImportedDeclaration;
}

}  // namespace zxdb
