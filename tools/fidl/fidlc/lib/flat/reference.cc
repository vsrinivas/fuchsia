// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/reference.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/raw_ast.h"

namespace fidl::flat {

Reference::Target::Target(Decl* decl) : target_(decl) {}
Reference::Target::Target(Element* member, Decl* parent) : target_(member), maybe_parent_(parent) {}

Name Reference::Target::name() const {
  switch (target_->kind) {
    case Element::Kind::kBits:
    case Element::Kind::kBuiltin:
    case Element::Kind::kConst:
    case Element::Kind::kEnum:
    case Element::Kind::kNewType:
    case Element::Kind::kProtocol:
    case Element::Kind::kResource:
    case Element::Kind::kService:
    case Element::Kind::kStruct:
    case Element::Kind::kTable:
    case Element::Kind::kAlias:
    case Element::Kind::kUnion:
      return target_->AsDecl()->name;
    case Element::Kind::kBitsMember:
      return maybe_parent_->name.WithMemberName(
          std::string(static_cast<Bits::Member*>(target_)->name.data()));
    case Element::Kind::kEnumMember:
      return maybe_parent_->name.WithMemberName(
          std::string(static_cast<Enum::Member*>(target_)->name.data()));
    default:
      ZX_PANIC("invalid element kind");
  }
}

const Library* Reference::Target::library() const {
  return element_or_parent_decl()->name.library();
}

Decl* Reference::Target::element_or_parent_decl() const {
  return maybe_parent_ ? maybe_parent_ : target_->AsDecl();
}

Reference::Reference(const raw::CompoundIdentifier& name) : span_(name.span()) {
  ZX_ASSERT_MSG(!name.components.empty(), "expected at least one component");
  auto& raw = std::get<RawSourced>(state_);
  for (auto& identifier : name.components) {
    raw.components.push_back(identifier->span().data());
  }
}

Reference::Reference(Target target) : state_(RawSynthetic{target}) {}

Reference::State Reference::state() const {
  return std::visit(fidl::utils::matchers{
                        [&](const RawSourced&) { return State::kRawSourced; },
                        [&](const RawSynthetic&) { return State::kRawSynthetic; },
                        [&](const Key&) { return State::kKey; },
                        [&](const Contextual&) { return State::kContextual; },
                        [&](const Target&) { return State::kResolved; },
                        [&](const Failed&) { return State::kFailed; },
                    },
                    state_);
}

void Reference::SetKey(Key key) {
  ZX_ASSERT_MSG(state() == State::kRawSourced || state() == State::kRawSynthetic, "invalid state");
  state_ = key;
}

void Reference::MarkContextual() {
  auto raw = raw_sourced();
  ZX_ASSERT_MSG(raw.components.size() == 1, "contextual requires 1 component");
  state_ = Contextual{raw.components[0]};
}

void Reference::ResolveTo(Target target) {
  ZX_ASSERT_MSG(state() == State::kKey || state() == State::kContextual, "invalid state");
  state_ = target;
}

void Reference::MarkFailed() {
  ZX_ASSERT_MSG(
      state() == State::kRawSourced || state() == State::kKey || state() == State::kContextual,
      "invalid state");
  state_ = Failed{};
}

}  // namespace fidl::flat
