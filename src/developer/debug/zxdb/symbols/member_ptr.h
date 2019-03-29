// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// Represents both pointers to member functions:
//    int (Foo::*)(double)
// (in which case the member_type() is a FunctionType), or a pointers to
// data members:
//    int Foo::*
// (in which case the pointer_type() is a some other type like "int").
class MemberPtr final : public Type {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const MemberPtr* AsMemberPtr() const override;

  // This is the containing class type, i.e. "Foo" in "int Foo::*".
  const LazySymbol& container_type() const { return container_type_; }

  // This is the type being pointed to (a FunctionType, int, etc.).
  const LazySymbol& member_type() const { return member_type_; }

 protected:
  // Symbol protected overrides.
  std::string ComputeFullName() const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(MemberPtr);
  FRIEND_MAKE_REF_COUNTED(MemberPtr);

  MemberPtr(LazySymbol container_type, LazySymbol member_type);
  virtual ~MemberPtr();

  LazySymbol container_type_;
  LazySymbol member_type_;
};

}  // namespace zxdb
