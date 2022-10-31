// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/sort_step.h"

#include <zircon/assert.h>

#include <algorithm>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/names.h"

namespace fidl::flat {

namespace {

// Compares decls by name lexicographically, then by availability.
struct CmpDeclName {
  bool operator()(const Decl* a, const Decl* b) const {
    if (a->name == b->name) {
      auto ar = a->availability.range();
      auto br = b->availability.range();
      ZX_ASSERT(ar != br || a == b);
      return ar < br;
    }
    // Avoid constructing the full name when libraries are the same (faster).
    if (a->name.library() == b->name.library()) {
      return a->name.decl_name() < b->name.decl_name();
    }
    return NameFlatName(a->name) < NameFlatName(b->name);
  }
};

// Helper class for calculating a decl's dependencies.
class CalcDependencies {
 public:
  explicit CalcDependencies(const Decl* decl);
  std::set<const Decl*, CmpDeclName> get() && { return std::move(dependencies_); }

 private:
  void AddDependency(const Decl* decl);
  void VisitConstant(const Constant* constant);
  void VisitTypeConstructor(const TypeConstructor* type_ctor);

  const Library* library_;
  std::set<const Decl*, CmpDeclName> dependencies_;
};

}  // namespace

void CalcDependencies::AddDependency(const Decl* decl) {
  // Only process decls from the current library.
  if (decl->name.library() == library_) {
    dependencies_.insert(decl);
  }
}

void CalcDependencies::VisitConstant(const Constant* constant) {
  switch (constant->kind) {
    case Constant::Kind::kIdentifier: {
      auto identifier = static_cast<const flat::IdentifierConstant*>(constant);
      AddDependency(identifier->reference.resolved().element_or_parent_decl());
      break;
    }
    case Constant::Kind::kLiteral: {
      // Literal and synthesized constants have no dependencies on other declarations.
      break;
    }
    case Constant::Kind::kBinaryOperator: {
      auto op = static_cast<const flat::BinaryOperatorConstant*>(constant);
      VisitConstant(op->left_operand.get());
      VisitConstant(op->right_operand.get());
      break;
    }
  }
}

void CalcDependencies::VisitTypeConstructor(const TypeConstructor* type_ctor) {
  const auto& invocation = type_ctor->resolved_params;
  if (invocation.from_alias) {
    ZX_ASSERT_MSG(!invocation.element_type_resolved, "partial aliases should be disallowed");
    AddDependency(invocation.from_alias);
    return;
  }

  const Type* type = type_ctor->type;
  if (type->nullability == types::Nullability::kNullable) {
    // We do not create edges for nullable types. This makes it possible to
    // write some recursive types in FIDL. However, we do not have comprehensive
    // support for recursive types. See fxbug.dev/35218 for details.
    return;
  }

  switch (type->kind) {
    case Type::Kind::kHandle: {
      auto handle_type = static_cast<const HandleType*>(type);
      ZX_ASSERT(handle_type->resource_decl);
      auto decl = static_cast<const Decl*>(handle_type->resource_decl);
      AddDependency(decl);
      break;
    }
    case Type::Kind::kPrimitive:
    case Type::Kind::kInternal:
    case Type::Kind::kString:
    case Type::Kind::kTransportSide:
    case Type::Kind::kBox:
      break;
    case Type::Kind::kArray:
    case Type::Kind::kVector:
    case Type::Kind::kZxExperimentalPointer: {
      if (invocation.element_type_raw != nullptr) {
        VisitTypeConstructor(invocation.element_type_raw);
      }
      // The type_ctor won't have an element_type_raw if the type is Bytes.
      // In that case, do nothing since there are no edges.
      break;
    }
    case Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      auto decl = static_cast<const Decl*>(identifier_type->type_decl);
      if (decl->kind != Decl::Kind::kProtocol) {
        AddDependency(decl);
      }
      break;
    }
    case Type::Kind::kUntypedNumeric:
      ZX_PANIC("should not have untyped numeric here");
  }
}

CalcDependencies::CalcDependencies(const Decl* decl) : library_(decl->name.library()) {
  switch (decl->kind) {
    case Decl::Kind::kBuiltin:
      break;
    case Decl::Kind::kBits: {
      auto bits_decl = static_cast<const Bits*>(decl);
      VisitTypeConstructor(bits_decl->subtype_ctor.get());
      for (const auto& member : bits_decl->members) {
        VisitConstant(member.value.get());
      }
      break;
    }
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<const Const*>(decl);
      VisitTypeConstructor(const_decl->type_ctor.get());
      VisitConstant(const_decl->value.get());
      break;
    }
    case Decl::Kind::kEnum: {
      auto enum_decl = static_cast<const Enum*>(decl);
      VisitTypeConstructor(enum_decl->subtype_ctor.get());
      for (const auto& member : enum_decl->members) {
        VisitConstant(member.value.get());
      }
      break;
    }
    case Decl::Kind::kProtocol: {
      auto protocol_decl = static_cast<const Protocol*>(decl);
      for (const auto& composed_protocol : protocol_decl->composed_protocols) {
        AddDependency(composed_protocol.reference.resolved().element()->AsDecl());
      }
      for (const auto& method : protocol_decl->methods) {
        if (method.maybe_request) {
          AddDependency(method.maybe_request->layout.resolved().element()->AsDecl());
        }
        if (method.maybe_response) {
          AddDependency(method.maybe_response->layout.resolved().element()->AsDecl());
        }
      }
      break;
    }
    case Decl::Kind::kResource: {
      auto resource_decl = static_cast<const Resource*>(decl);
      VisitTypeConstructor(resource_decl->subtype_ctor.get());
      break;
    }
    case Decl::Kind::kService: {
      auto service_decl = static_cast<const Service*>(decl);
      for (const auto& member : service_decl->members) {
        VisitTypeConstructor(member.type_ctor.get());
      }
      break;
    }
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(decl);
      for (const auto& member : struct_decl->members) {
        VisitTypeConstructor(member.type_ctor.get());
        if (member.maybe_default_value) {
          VisitConstant(member.maybe_default_value.get());
        }
      }
      break;
    }
    case Decl::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(decl);
      for (const auto& member : table_decl->members) {
        if (!member.maybe_used) {
          continue;
        }
        VisitTypeConstructor(member.maybe_used->type_ctor.get());
      }
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(decl);
      for (const auto& member : union_decl->members) {
        if (!member.maybe_used) {
          continue;
        }
        VisitTypeConstructor(member.maybe_used->type_ctor.get());
      }
      break;
    }
    case Decl::Kind::kAlias: {
      auto alias_decl = static_cast<const Alias*>(decl);
      VisitTypeConstructor(alias_decl->partial_type_ctor.get());
      break;
    }
    case Decl::Kind::kNewType: {
      auto new_type_decl = static_cast<const NewType*>(decl);
      VisitTypeConstructor(new_type_decl->type_ctor.get());
      break;
    }
  }
}

// Recursive helper for building a cycle to use as the example in
// ErrIncludeCycle. Given |dependencies|, the set of remaining undeclared
// dependencies of each decl and |inverse_dependencies|, the list other decls
// which depend on each decl, build out |cycle| by recursively searching all
// dependents of the last element in the cycle until one is found which is
// already a member of the cycle.
//
// The cycle must not be empty, as the last element of the cycle is the current
// search point in the recursion.
static bool BuildExampleCycle(
    std::map<const Decl*, std::set<const Decl*, CmpDeclName>, CmpDeclName>& dependencies,
    std::vector<const Decl*>& cycle) {
  const Decl* const decl = cycle.back();
  for (const Decl* dep : dependencies[decl]) {
    auto dep_pos = std::find(cycle.begin(), cycle.end(), dep);
    if (dep_pos != cycle.end()) {
      // We found a decl that is in the cycle already. That means we have a
      // cycle from dep_pos to cycle.end(), but there may be other decls from
      // cycle.begin() to decl_pos which are either leaf elements not part of
      // this cycle or part of another cycle which intersects this one.
      cycle.erase(cycle.begin(), dep_pos);
      // Add another reference to the same decl so it gets printed at both the
      // start and end of the cycle.
      cycle.push_back(dep);
      return true;
    }
    cycle.push_back(dep);
    if (BuildExampleCycle(dependencies, cycle)) {
      return true;
    }
    cycle.pop_back();
  }
  return false;
}

void SortStep::RunImpl() {
  // |dependences| is the number of undeclared dependencies left for each decl.
  std::map<const Decl*, std::set<const Decl*, CmpDeclName>, CmpDeclName> dependencies;
  // |inverse_dependencies| records the decls that depend on each decl.
  std::map<const Decl*, std::vector<const Decl*>, CmpDeclName> inverse_dependencies;
  for (const auto& [name, decl] : library()->declarations.all) {
    auto deps = CalcDependencies(decl).get();
    for (const Decl* dep : deps) {
      inverse_dependencies[dep].push_back(decl);
    }
    dependencies[decl] = std::move(deps);
  }

  // Start with all decls that have no incoming edges.
  std::vector<const Decl*> decls_without_deps;
  for (const auto& [decl, deps] : dependencies) {
    if (deps.empty()) {
      decls_without_deps.push_back(decl);
    }
  }

  while (!decls_without_deps.empty()) {
    // Pull one out of the queue.
    auto decl = decls_without_deps.back();
    decls_without_deps.pop_back();
    ZX_ASSERT(dependencies[decl].empty());
    library()->declaration_order.push_back(decl);

    // Since this decl is now declared, remove it from the set of undeclared
    // dependencies for every other decl that depends on it.
    auto& inverse_deps = inverse_dependencies[decl];
    for (const Decl* inverse_dep : inverse_deps) {
      auto& inverse_dep_forward_edges = dependencies[inverse_dep];
      auto incoming_edge = inverse_dep_forward_edges.find(decl);
      ZX_ASSERT(incoming_edge != inverse_dep_forward_edges.end());
      inverse_dep_forward_edges.erase(incoming_edge);
      if (inverse_dep_forward_edges.empty())
        decls_without_deps.push_back(inverse_dep);
    }
  }

  if (library()->declaration_order.size() != dependencies.size()) {
    // We didn't visit all the edges! There was a cycle.

    // Find a cycle to use as an example in the error message.  We start from
    // some type that still has undeclared outgoing dependencies, then do a DFS
    // until we get back to a type we've visited before.
    std::vector<const Decl*> cycle;
    for (auto const& [decl, deps] : dependencies) {
      // Find the first type that still has undeclared deps.
      if (!deps.empty()) {
        cycle.push_back(decl);
        break;
      }
    }
    // There is a cycle so we should find at least one decl with remaining
    // undeclared deps.
    ZX_ASSERT(!cycle.empty());
    // Because there is a cycle, building a cycle should always succeed.
    bool found_cycle = BuildExampleCycle(dependencies, cycle);
    ZX_ASSERT(found_cycle);
    // Even if there is only one element in the cycle (a self-loop),
    // BuildExampleCycle should add a second entry so when printing we get A->A,
    // so the list should always end up with at least two elements.
    ZX_ASSERT(cycle.size() > 1);

    Fail(ErrIncludeCycle, cycle.front()->name.span().value(), cycle);
  }
}

}  // namespace fidl::flat
