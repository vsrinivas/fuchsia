// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/collection.h"

#include "lib/fxl/strings/string_printf.h"

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

std::string Collection::ComputeFullName() const {
  // Some compiler-generated classes have no names. Clang does this for the
  // implicit classes that hold closure values. So provide a better description
  // when those are printed. This isn't qualified with namespaces because that
  // doesn't add much value when there's no name.
  const std::string& assigned_name = GetAssignedName();
  if (assigned_name.empty())
    return fxl::StringPrintf("(anon %s)", GetKindString());
  return Symbol::ComputeFullName();
}

}  // namespace zxdb
