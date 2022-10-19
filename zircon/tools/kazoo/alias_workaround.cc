// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/syscall_library.h"

// See alias_workarounds[.test].fidl for explanation and what these will be in "real" .fidl once the
// frontend supports the necessary syntax.
bool AliasWorkaround(const std::string& name, const SyscallLibrary& library, Type* type) {
  if (name == "ConstFutexPtr") {
    *type = Type(TypePointer(Type(TypeZxBasicAlias("Futex"))), Constness::kConst);
    return true;
  }
  if (name == "VectorPaddr") {
    *type = Type(TypeVector(Type(TypeZxBasicAlias("paddr"))), Constness::kConst);
    return true;
  }
  return false;
}
