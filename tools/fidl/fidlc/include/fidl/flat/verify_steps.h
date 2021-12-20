// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VERIFY_STEPS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VERIFY_STEPS_H_

#include "fidl/flat/step_base.h"
#include "fidl/types.h"

namespace fidl::flat {

struct Attributable;

class VerifyResourcenessStep : public StepBase {
 public:
  using StepBase::StepBase;

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

class VerifyAttributesStep : public StepBase {
 public:
  using StepBase::StepBase;

 private:
  void RunImpl() override;
  void VerifyDecl(const Decl* decl);
  void VerifyAttributes(const Attributable* attributable);
};

class VerifyInlineSizeStep : public StepBase {
 public:
  using StepBase::StepBase;

 private:
  void RunImpl() override;
};

class VerifyDependenciesStep : public StepBase {
 public:
  using StepBase::StepBase;

 private:
  void RunImpl() override;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VERIFY_STEPS_H_
