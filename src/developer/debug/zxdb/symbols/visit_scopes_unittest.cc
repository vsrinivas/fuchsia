// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/inheritance_path.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"

namespace zxdb {

TEST(VisitScopes, ClassHierarchy) {
  auto base1 = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  auto mid1 = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  auto mid2 = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  auto derived = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);

  // Stores the collections and their paths visited.
  using VisitLog = std::vector<InheritancePath>;
  VisitLog visited;

  // A single class with no hierarchy.
  VisitResult result =
      VisitClassHierarchy(derived.get(), [&visited](const InheritancePath& path) {
        visited.push_back(path);
        return VisitResult::kContinue;
      });
  EXPECT_EQ(VisitResult::kContinue, result);
  VisitLog expected{{{derived}}};
  EXPECT_EQ(expected, visited);

  // Complex hierarchy:
  //   base1 -- mid1 --
  //                   \
  //            mid2 ------ derived
  uint64_t mid1_offset = 8;
  uint64_t mid2_offset = 0;
  uint64_t base1_offset = 32;
  auto mid1_inh = fxl::MakeRefCounted<InheritedFrom>(mid1, mid1_offset);
  auto mid2_inh = fxl::MakeRefCounted<InheritedFrom>(mid2, mid2_offset);
  auto base1_inh = fxl::MakeRefCounted<InheritedFrom>(base1, base1_offset);
  derived->set_inherited_from({LazySymbol(mid1_inh), LazySymbol(mid2_inh)});
  mid1->set_inherited_from({LazySymbol(base1_inh)});

  // Visit all of those, they're visited in depth-first-search order (the ordering was most
  // convenient for the implementation, it can be changed in the future if there's a reason for a
  // specific different order).
  visited = VisitLog();
  result = VisitClassHierarchy(derived.get(), [&visited](const InheritancePath& path) {
    visited.push_back(path);
    return VisitResult::kContinue;
  });
  EXPECT_EQ(VisitResult::kContinue, result);
  expected = VisitLog{{{derived}},
                      {{derived}, {mid1_inh, mid1}},
                      {{derived}, {mid1_inh, mid1}, {base1_inh, base1}},
                      {{derived}, {mid2_inh, mid2}}};
  EXPECT_EQ(expected, visited);

  // Test early termination at mid1.
  visited = VisitLog();
  result = VisitClassHierarchy(derived.get(), [&visited, mid1](const InheritancePath& path) {
    visited.emplace_back(path);
    return path.base() == mid1.get() ? VisitResult::kDone : VisitResult::kContinue;
  });
  EXPECT_EQ(VisitResult::kDone, result);  // Should have found mid1.
  expected = VisitLog{{{derived}},
                      {{derived}, {mid1_inh, mid1}}};
  EXPECT_EQ(expected, visited);
}

}  // namespace zxdb
