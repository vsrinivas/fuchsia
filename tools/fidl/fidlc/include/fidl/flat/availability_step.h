// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_AVAILABILITY_STEP_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_AVAILABILITY_STEP_H_

#include "tools/fidl/fidlc/include/fidl/flat/compiler.h"

namespace fidl::flat {

// The AvailabilityStep sets element->availability for every element in the
// library based on @available attributes and inheritance rules. If the library
// is versioned, it sets library->platform. Otherwise, it leaves it null, and
// all element availabilities will be unbounded. This step also checks for name
// collisions on overlapping availabilities for top level declarations (but not
// their members; they are checked in the CompileStep).
class AvailabilityStep : public Compiler::Step {
 public:
  using Step::Step;

 private:
  void RunImpl() override;

  void PopulateLexicalParents();

  // Sets `element->availability` from the @available attribute, inheriting
  // unset fields from `AvailabilityToInheritFrom(element)`.
  void CompileAvailability(Element* element);
  // Helper function for `CompileAvailability`.
  void CompileAvailabilityFromAttribute(Element* element, Attribute* attribute);

  // Returns the default platform (the first component of the library name).
  Platform GetDefaultPlatform();
  // Parses the argument value as a platform. Reports an error on failure.
  std::optional<Platform> GetPlatform(const AttributeArg* maybe_arg);
  // Parses the argument value as a version. Reports an error on failure.
  std::optional<Version> GetVersion(const AttributeArg* maybe_arg);
  // Parses the argument value as a legacy status. Reports an error on failure.
  std::optional<Availability::Legacy> GetLegacy(const AttributeArg* maybe_arg);

  // Returns the availability that `element` should inherit from, or null
  // if it should not attempt inheriting.
  std::optional<Availability> AvailabilityToInheritFrom(const Element* element);

  // Given an argument name, returns the nearest ancestor argument that
  // `element` inherited its value from. Requires that such an argument exists.
  // For example, consider this FIDL:
  //
  //     1 | @available(added=2)     // <-- ancestor
  //     2 | library test;
  //     3 | type Foo = struct {
  //     4 |    @available(added=1)  // <-- arg
  //     5 |    bar uint32;
  //     6 | };
  //
  // The `added=2` flows from `library test` to `type Foo` to `bar uint32`. But
  // we want the error ("can't add bar at version 1 when its parent isn't added
  // until version 2") to point to line 1, not to line 3.
  const AttributeArg* AncestorArgument(const Element* element, std::string_view arg_name);

  // Returns the lexical parent of `element`, or null for the root.
  //
  // The lexical parent differs from the scope in which an `element` exists
  // in the case of anonymous layouts: the lexical parent is the direct
  // container in which an `element` was defined, whereas they are hoisted
  // to library-scope. For example:
  //
  //     @available(added=1)
  //     library test;            // scope: null,    lexical parent: null
  //     @available(added=2)
  //     type Foo = struct {      // scope: library, lexical parent: library
  //         @available(added=3)
  //         bar                  // scope: Foo,     lexical parent: Foo
  //             struct {};       // scope: library, lexical parent: bar
  //     };
  //
  // After consuming the raw AST, the anonymous layout `struct {}` gets treated
  // like a top-level declaration alongside `Foo`. But we inherit from its
  // lexical parent, the member `bar` (added at version 3).
  Element* LexicalParent(const Element* element);

  // Reports errors for all decl name collisions on overlapping availabilities.
  void VerifyNoDeclOverlaps();

  // Maps members to the Decl they occur in, and anonymous layouts to the
  // struct/table/union member whose type constructor they occur in.
  std::map<const Element*, Element*> lexical_parents_;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_AVAILABILITY_STEP_H_
