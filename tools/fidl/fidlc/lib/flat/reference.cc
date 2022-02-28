// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/reference.h"

#include "fidl/flat_ast.h"
#include "fidl/raw_ast.h"

namespace fidl::flat {

Reference::Reference(const raw::CompoundIdentifier& name) : span_(name.span()) {
  assert(!name.components.empty() && "expected at least one component");
  for (auto& identifier : name.components) {
    components_.push_back(identifier->span().data());
  }
}

Reference::Reference(Decl* target) : target_(target) {}

void Reference::Resolve(Element* target, Decl* maybe_parent) {
  switch (target->kind) {
    case Element::Kind::kBitsMember:
    case Element::Kind::kEnumMember:
      assert(maybe_parent && "must provide parent when target is a member");
      break;
    default:
      assert(!maybe_parent && "unexpected parent decl");
      break;
  }
  assert(target_ == nullptr);
  assert(maybe_parent_ == nullptr);
  target_ = target;
  maybe_parent_ = maybe_parent;
}

Name Reference::target_name() const {
  assert(target_ && "reference is not resolved");
  switch (target_->kind) {
    case Element::Kind::kBits:
    case Element::Kind::kBuiltin:
    case Element::Kind::kConst:
    case Element::Kind::kEnum:
    case Element::Kind::kProtocol:
    case Element::Kind::kResource:
    case Element::Kind::kService:
    case Element::Kind::kStruct:
    case Element::Kind::kTable:
    case Element::Kind::kTypeAlias:
    case Element::Kind::kUnion:
      return target_->AsDecl()->name;
    case Element::Kind::kBitsMember:
      return maybe_parent_->name.WithMemberName(
          std::string(static_cast<Bits::Member*>(target_)->name.data()));
    case Element::Kind::kEnumMember:
      return maybe_parent_->name.WithMemberName(
          std::string(static_cast<Enum::Member*>(target_)->name.data()));
    default:
      assert(false && "invalid element kind");
      __builtin_unreachable();
  }
}

const Library* Reference::target_library() const { return target_or_parent_decl()->name.library(); }

Decl* Reference::target_or_parent_decl() const {
  assert(target_ && "reference is not resolved");
  if (maybe_parent_) {
    return maybe_parent_;
  }
  return target_->AsDecl();
}

}  // namespace fidl::flat
