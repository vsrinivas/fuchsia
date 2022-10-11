// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_RESOLVE_STEP_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_RESOLVE_STEP_H_

#include "tools/fidl/fidlc/include/fidl/flat/compiler.h"

namespace fidl::flat {

// This step resolves all references in the library. It does so in three steps:
//
// 1. Parse the structure of each reference. For example, given `foo.bar`, this
//    means choosing between "library foo, decl bar" and "decl foo, member bar".
//    This step does not consult availabilities nor the version selection.
// 2. Perform temporal decomposition, splitting declarations into finer-grained
//    pieces such that for each one, nothing changes over its availability.
// 3. Resolve all references in the decomposed AST, linking each one to the
//    specific Element* it refers to.
//
// Note that ResolveStep does not resolve constant values (i.e. calling
// Constant::ResolveTo). That happens in the CompileStep.
class ResolveStep : public Compiler::Step {
 public:
  using Step::Step;

 private:
  void RunImpl() override;

  // Context controls dynamic behavior during traversals of all references.
  struct Context {
    enum struct Mode {
      // Calls ParseReference and InsertReferenceEdges.
      kParseAndInsert,
      // Calls ResolveReference and ValidateReference.
      kResolveAndValidate,
    };

    explicit Context(Mode mode, Element* enclosing) : mode(mode), enclosing(enclosing) {}

    // What to do when we reach leaves (references).
    Mode mode;
    // Element that the reference occurs in.
    Element* enclosing;
    // Used in kParseAndInsert. If true, we call Reference::MarkContextual
    // instead of Reference::MarkFailed for a single-component reference,
    // deferring the final contextual lookup to kResolve.
    bool allow_contextual = false;
    // Used in kResolve. If non-null, we look up contextual names in this enum.
    // This enables, for example, `zx.handle:CHANNEL` as a shorthand for
    // `zx.handle:zx.obj_type.CHANNEL` (here the enum is `zx.obj_type`).
    Enum* maybe_resource_subtype = nullptr;
  };

  class Lookup;

  void VisitElement(Element* element, Context context);
  void VisitTypeConstructor(TypeConstructor* type_ctor, Context context);
  void VisitConstant(Constant* constant, Context context);
  void VisitReference(Reference& ref, Context context);

  // Calls ref.SetKey, ref.MarkContextual, or ref.MarkFailed.
  void ParseReference(Reference& ref, Context context);
  // Helpers for ParseReference.
  void ParseSyntheticReference(Reference& ref, Context context);
  void ParseSourcedReference(Reference& ref, Context context);
  // Inserts edges into graph_ for a parsed reference.
  void InsertReferenceEdges(const Reference& ref, Context context);

  // Calls ref.ResolveTo or ref.MarkFailed.
  void ResolveReference(Reference& ref, Context context);
  // Helpers for ResolveReference.
  void ResolveContextualReference(Reference& ref, Context context);
  void ResolveKeyReference(Reference& ref, Context context);
  Decl* LookupDeclByKey(const Reference& ref, Context context);
  // Validates a resolved reference (e.g. checks deprecation rules).
  void ValidateReference(const Reference& ref, Context context);

  // Returns an augmented context to use when visiting type_ctor's constraints.
  Context ConstraintContext(const TypeConstructor* type_ctor, Context context);

  // Per-node information for the version graph.
  struct NodeInfo {
    // Set of points at which to split this element in the final decomposition.
    // It initially contains 2 endpoints (or 3 points with deprecation), and
    // then receives more points from incoming neighbours.
    std::set<Version> points;
    // Set of outgoing neighbors. These are either *membership edges* (from
    // child to parent, e.g. struct member to struct) or *reference edges* (from
    // declaration to use, e.g. struct to table member carrying the struct).
    std::set<const Element*> neighbors;
  };

  // The version graph for this library: directed, possibly cyclic, possibly
  // disconnected. Contains only elements from the current library's platform:
  // all the current library's elements, plus elements from external libraries
  // it references. The latter have in-degree zero, i.e. they only appear as map
  // keys and never in the sets of outgoing neighbors.
  std::map<const Element*, NodeInfo> graph_;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_RESOLVE_STEP_H_
