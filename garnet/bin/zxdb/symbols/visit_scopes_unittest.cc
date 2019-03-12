// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/visit_scopes.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/inherited_from.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(VisitScopes, ClassHierarchy) {
  auto base1 = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  auto mid1 = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  auto mid2 = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  auto derived = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);

  // Stores the collections and their offsets visited.
  using VisitLog = std::vector<std::pair<const Collection*, uint32_t>>;
  VisitLog visited;

  // A single class with no hierarchy.
  bool found = VisitClassHierarchy(
      derived.get(), [&visited](const Collection* c, uint32_t o) -> bool {
        visited.emplace_back(c, o);
        return false;
      });
  EXPECT_FALSE(found);  // All callbacks returned false.
  VisitLog expected{{derived.get(), 0}};
  EXPECT_EQ(expected, visited);

  // Complex hierarchy:
  //   base1 -- mid1 --
  //                   \
  //            mid2 ------ derived
  uint32_t mid1_offset = 8;
  uint32_t mid2_offset = 0;
  uint32_t base1_offset = 32;
  auto mid1_inh =
      fxl::MakeRefCounted<InheritedFrom>(LazySymbol(mid1), mid1_offset);
  auto mid2_inh =
      fxl::MakeRefCounted<InheritedFrom>(LazySymbol(mid2), mid2_offset);
  auto base1_inh =
      fxl::MakeRefCounted<InheritedFrom>(LazySymbol(base1), base1_offset);
  derived->set_inherited_from({LazySymbol(mid1_inh), LazySymbol(mid2_inh)});
  mid1->set_inherited_from({LazySymbol(base1_inh)});

  // Visit all of those, they're visited in depth-first-search order (the
  // ordering was most convenient for the implementation, it can be changed
  // in the future if there's a reason for a specific different order).
  visited = VisitLog();
  found = VisitClassHierarchy(
      derived.get(), [&visited](const Collection* c, uint32_t o) -> bool {
        visited.emplace_back(c, o);
        return false;
      });
  EXPECT_FALSE(found);  // All callbacks returned false.
  expected = VisitLog{{derived.get(), 0},
                      {mid1.get(), mid1_offset},
                      {base1.get(), mid1_offset + base1_offset},
                      {mid2.get(), mid2_offset}};
  EXPECT_EQ(expected, visited);

  // Test early termination at mid1.
  visited = VisitLog();
  found = VisitClassHierarchy(
      derived.get(), [&visited, mid1](const Collection* c, uint32_t o) -> bool {
        visited.emplace_back(c, o);
        return c == mid1.get();
      });
  EXPECT_TRUE(found);  // Should have found mid1.
  expected = VisitLog{{derived.get(), 0}, {mid1.get(), mid1_offset}};
  EXPECT_EQ(expected, visited);
}

}  // namespace zxdb
