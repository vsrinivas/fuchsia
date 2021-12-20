// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/sort_step.h"

#include "fidl/names.h"

namespace fidl::flat {

using namespace diagnostics;

bool SortStep::AddConstantDependencies(const Constant* constant, std::set<const Decl*>* out_edges) {
  switch (constant->kind) {
    case Constant::Kind::kIdentifier: {
      auto identifier = static_cast<const flat::IdentifierConstant*>(constant);
      auto decl = library_->LookupDeclByName(identifier->name.memberless_key());
      if (decl == nullptr) {
        return Fail(ErrFailedConstantLookup, identifier->name.span().value(), identifier->name);
      }
      out_edges->insert(decl);
      break;
    }
    case Constant::Kind::kLiteral: {
      // Literal and synthesized constants have no dependencies on other declarations.
      break;
    }
    case Constant::Kind::kBinaryOperator: {
      auto op = static_cast<const flat::BinaryOperatorConstant*>(constant);
      return AddConstantDependencies(op->left_operand.get(), out_edges) &&
             AddConstantDependencies(op->right_operand.get(), out_edges);
    }
  }
  return true;
}

// Calculating declaration dependencies is largely serving the C/C++ family of languages bindings.
// For instance, the declaration of a struct member type must be defined before the containing
// struct if that member is stored inline.
// Given the FIDL declarations:
//
//     struct D2 { D1 d; }
//     struct D1 { int32 x; }
//
// We must first declare D1, followed by D2 when emitting C code.
//
// Below, an edge from D1 to D2 means that we must see the declaration of of D1 before
// the declaration of D2, i.e. the calculated set of |out_edges| represents all the declarations
// that |decl| depends on.
//
// Notes:
// - Nullable structs do not require dependency edges since they are boxed via a
// pointer indirection, and their content placed out-of-line.
bool SortStep::DeclDependencies(const Decl* decl, std::set<const Decl*>* out_edges) {
  std::set<const Decl*> edges;

  auto maybe_add_decl = [&edges](const TypeConstructor* type_ctor) {
    const TypeConstructor* current = type_ctor;
    for (;;) {
      const auto& invocation = current->resolved_params;
      if (invocation.from_type_alias) {
        assert(!invocation.element_type_resolved &&
               "Compiler bug: partial aliases should be disallowed");
        edges.insert(invocation.from_type_alias);
        return;
      }

      const Type* type = current->type;
      if (type->nullability == types::Nullability::kNullable)
        return;

      switch (type->kind) {
        case Type::Kind::kHandle: {
          auto handle_type = static_cast<const HandleType*>(type);
          assert(handle_type->resource_decl);
          auto decl = static_cast<const Decl*>(handle_type->resource_decl);
          edges.insert(decl);
          return;
        }
        case Type::Kind::kPrimitive:
        case Type::Kind::kString:
        case Type::Kind::kTransportSide:
        case Type::Kind::kBox:
          return;
        case Type::Kind::kArray:
        case Type::Kind::kVector: {
          if (invocation.element_type_raw != nullptr) {
            current = invocation.element_type_raw;
            break;
          }
          // The type_ctor won't have an arg_type_ctor if the type is Bytes.
          // In that case, just return since there are no edges
          return;
        }
        case Type::Kind::kIdentifier: {
          // should have been caught above and returned early.
          assert(type->nullability != types::Nullability::kNullable);
          auto identifier_type = static_cast<const IdentifierType*>(type);
          auto decl = static_cast<const Decl*>(identifier_type->type_decl);
          if (decl->kind != Decl::Kind::kProtocol) {
            edges.insert(decl);
          }
          return;
        }
        case Type::Kind::kUntypedNumeric:
          assert(false && "compiler bug: should not have untyped numeric here");
      }
    }
  };

  switch (decl->kind) {
    case Decl::Kind::kBits: {
      auto bits_decl = static_cast<const Bits*>(decl);
      maybe_add_decl(bits_decl->subtype_ctor.get());
      for (const auto& member : bits_decl->members) {
        if (!AddConstantDependencies(member.value.get(), &edges)) {
          return false;
        }
      }
      break;
    }
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<const Const*>(decl);
      maybe_add_decl(const_decl->type_ctor.get());
      if (!AddConstantDependencies(const_decl->value.get(), &edges)) {
        return false;
      }
      break;
    }
    case Decl::Kind::kEnum: {
      auto enum_decl = static_cast<const Enum*>(decl);
      maybe_add_decl(enum_decl->subtype_ctor.get());
      for (const auto& member : enum_decl->members) {
        if (!AddConstantDependencies(member.value.get(), &edges)) {
          return false;
        }
      }
      break;
    }
    case Decl::Kind::kProtocol: {
      auto protocol_decl = static_cast<const Protocol*>(decl);
      for (const auto& composed_protocol : protocol_decl->composed_protocols) {
        if (auto type_decl = library_->LookupDeclByName(composed_protocol.name); type_decl) {
          edges.insert(type_decl);
        }
      }
      for (const auto& method : protocol_decl->methods) {
        if (method.maybe_request != nullptr) {
          if (auto type_decl = library_->LookupDeclByName(method.maybe_request->name); type_decl) {
            edges.insert(type_decl);
          }
        }
        if (method.maybe_response != nullptr) {
          if (auto type_decl = library_->LookupDeclByName(method.maybe_response->name); type_decl) {
            edges.insert(type_decl);
          }
        }
      }
      break;
    }
    case Decl::Kind::kResource: {
      auto resource_decl = static_cast<const Resource*>(decl);
      maybe_add_decl(resource_decl->subtype_ctor.get());
      break;
    }
    case Decl::Kind::kService: {
      auto service_decl = static_cast<const Service*>(decl);
      for (const auto& member : service_decl->members) {
        maybe_add_decl(member.type_ctor.get());
      }
      break;
    }
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(decl);
      for (const auto& member : struct_decl->members) {
        maybe_add_decl(member.type_ctor.get());
        if (member.maybe_default_value) {
          if (!AddConstantDependencies(member.maybe_default_value.get(), &edges)) {
            return false;
          }
        }
      }
      break;
    }
    case Decl::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(decl);
      for (const auto& member : table_decl->members) {
        if (!member.maybe_used)
          continue;
        maybe_add_decl(member.maybe_used->type_ctor.get());
        if (member.maybe_used->maybe_default_value) {
          if (!AddConstantDependencies(member.maybe_used->maybe_default_value.get(), &edges)) {
            return false;
          }
        }
      }
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(decl);
      for (const auto& member : union_decl->members) {
        if (!member.maybe_used)
          continue;
        maybe_add_decl(member.maybe_used->type_ctor.get());
      }
      break;
    }
    case Decl::Kind::kTypeAlias: {
      auto type_alias_decl = static_cast<const TypeAlias*>(decl);
      maybe_add_decl(type_alias_decl->partial_type_ctor.get());
    }
  }  // switch
  *out_edges = std::move(edges);
  return true;
}

namespace {

// Declaration comparator.
//
// (1) To compare two Decl's in the same library, it suffices to compare the
//     unqualified names of the Decl's. (This is faster.)
//
// (2) To compare two Decl's across libraries, we rely on the fully qualified
//     names of the Decl's. (This is slower.)
struct CmpDeclInLibrary {
  bool operator()(const Decl* a, const Decl* b) const {
    assert(a->name != b->name || a == b);
    const Library* a_library = a->name.library();
    const Library* b_library = b->name.library();
    if (a_library != b_library) {
      return NameFlatName(a->name) < NameFlatName(b->name);
    } else {
      return a->name.decl_name() < b->name.decl_name();
    }
  }
};

}  // namespace

void SortStep::RunImpl() {
  // |degree| is the number of undeclared dependencies for each decl.
  std::map<const Decl*, uint32_t, CmpDeclInLibrary> degrees;
  // |inverse_dependencies| records the decls that depend on each decl.
  std::map<const Decl*, std::vector<const Decl*>, CmpDeclInLibrary> inverse_dependencies;
  for (const auto& name_and_decl : library_->declarations_) {
    const Decl* decl = name_and_decl.second;
    std::set<const Decl*> deps;
    if (!DeclDependencies(decl, &deps))
      return;
    degrees[decl] = static_cast<uint32_t>(deps.size());
    for (const Decl* dep : deps) {
      inverse_dependencies[dep].push_back(decl);
    }
  }

  // Start with all decls that have no incoming edges.
  std::vector<const Decl*> decls_without_deps;
  for (const auto& decl_and_degree : degrees) {
    if (decl_and_degree.second == 0u) {
      decls_without_deps.push_back(decl_and_degree.first);
    }
  }

  while (!decls_without_deps.empty()) {
    // Pull one out of the queue.
    auto decl = decls_without_deps.back();
    decls_without_deps.pop_back();
    assert(degrees[decl] == 0u);
    library_->declaration_order_.push_back(decl);

    // Decrement the incoming degree of all the other decls it
    // points to.
    auto& inverse_deps = inverse_dependencies[decl];
    for (const Decl* inverse_dep : inverse_deps) {
      uint32_t& degree = degrees[inverse_dep];
      assert(degree != 0u);
      degree -= 1;
      if (degree == 0u)
        decls_without_deps.push_back(inverse_dep);
    }
  }

  if (library_->declaration_order_.size() != degrees.size()) {
    // We didn't visit all the edges! There was a cycle.
    library_->FailNoSpan(ErrIncludeCycle);
  }
}

}  // namespace fidl::flat
