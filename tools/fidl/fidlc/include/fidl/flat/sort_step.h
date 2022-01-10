// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_SORT_STEP_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_SORT_STEP_H_

#include "fidl/flat/step_base.h"

namespace fidl::flat {

struct Constant;
struct Decl;

class SortStep : public StepBase {
 public:
  using StepBase::StepBase;

 private:
  struct CmpDeclInLibrary {
    bool operator()(const Decl* a, const Decl* b) const;
  };

  void RunImpl() override;
  bool AddConstantDependencies(const Constant* constant,
                               std::set<const Decl*, CmpDeclInLibrary>* out_edges);
  bool DeclDependencies(const Decl* decl, std::set<const Decl*, CmpDeclInLibrary>* out_edges);
  static bool BuildExampleCycle(std::map<const Decl*, std::set<const Decl*, CmpDeclInLibrary>,
                                         CmpDeclInLibrary>& dependencies,
                                std::vector<const Decl*>& cycle);
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_SORT_STEP_H_
