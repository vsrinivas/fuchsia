// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/collection.h"

namespace zxdb {

Collection::Collection(int tag) : Type(tag) {}
Collection::~Collection() = default;

const Collection* Collection::AsCollection() const { return this; }

const char* Collection::GetKindString() const {
  switch (tag()) {
    case kTagStructureType:
      return "struct";
    case kTagClassType:
      return "class";
    case kTagUnionType:
      return "union";
    default:
      return "unknown";
  }
}

}  // namespace zxdb
