// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/member_ptr.h"

#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/function_type.h"

namespace zxdb {

MemberPtr::MemberPtr(LazySymbol container_type, LazySymbol member_type)
    : Type(DwarfTag::kPtrToMemberType),
      container_type_(std::move(container_type)),
      member_type_(std::move(member_type)) {
  set_byte_size(kTargetPointerSize);
}

MemberPtr::~MemberPtr() = default;

const MemberPtr* MemberPtr::AsMemberPtr() const { return this; }

std::string MemberPtr::ComputeFullName() const {
  const Type* member = member_type_.Get()->AsType();
  if (!member)
    return "<invalid>";

  std::string container_name;
  if (const Type* container = container_type_.Get()->AsType()) {
    container_name = container->GetFullName();
  } else {
    // Can still compute function description from the type when the container
    // is bad.
    container_name = "<invalid>";
  }

  // Special-case pointer-to-member-functions.
  if (const FunctionType* func = member->AsFunctionType())
    return func->ComputeFullNameForFunctionPtr(container_name);

  // Everything else is a pointer to member data.
  return member->GetFullName() + " " + container_name + "::*";
}

}  // namespace zxdb
