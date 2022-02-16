// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VERIFY_STEPS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VERIFY_STEPS_H_

#include "fidl/flat/compiler.h"

namespace fidl::flat {

struct Element;

class VerifyResourcenessStep : public Compiler::Step {
 public:
  using Step::Step;

 private:
  void RunImpl() override;
  void VerifyDecl(const Decl* decl);

  // Returns the effective resourceness of |type|. The set of effective resource
  // types includes (1) nominal resource types per the FTP-057 definition, and
  // (2) declarations that have an effective resource member (or equivalently,
  // transitively contain a nominal resource).
  types::Resourceness EffectiveResourceness(const Type* type);

  // Map from struct/table/union declarations to their effective resourceness. A
  // value of std::nullopt indicates that the declaration has been visited, used
  // to prevent infinite recursion.
  std::map<const Decl*, std::optional<types::Resourceness>> effective_resourceness_;
};

class VerifyAttributesStep : public Compiler::Step {
 public:
  using Step::Step;

 private:
  void RunImpl() override;
  void VerifyAttributes(const Element* element);
};

class VerifyInlineSizeStep : public Compiler::Step {
 public:
  using Step::Step;

 private:
  void RunImpl() override;
};

class VerifyDependenciesStep : public Compiler::Step {
 public:
  using Step::Step;

 private:
  void RunImpl() override;
};

class VerifyOpenInteractionsStep : public Compiler::Step {
 public:
  using Step::Step;

 private:
  void RunImpl() override;
  void VerifyProtocolOpenness(const Protocol& protocol);
  static bool IsAllowedComposition(types::Openness composing, types::Openness composed);
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VERIFY_STEPS_H_
