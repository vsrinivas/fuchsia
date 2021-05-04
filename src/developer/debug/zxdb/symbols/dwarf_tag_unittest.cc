// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(DwarfTag, DwarfTagToString) {
  // Spot check a few to make sure the string array is in-sync.
  EXPECT_EQ("<none>", DwarfTagToString(DwarfTag::kNone));
  EXPECT_EQ("DW_TAG_array_type (0x01)", DwarfTagToString(DwarfTag::kArrayType, true));
  EXPECT_EQ("<undefined (0x14)>", DwarfTagToString(static_cast<DwarfTag>(0x14)));
  EXPECT_EQ("DW_TAG_enumerator", DwarfTagToString(DwarfTag::kEnumerator));
  EXPECT_EQ("DW_TAG_template_alias", DwarfTagToString(DwarfTag::kTemplateAlias));  // Last DWARF 4.
  EXPECT_EQ("DW_TAG_immutable_type", DwarfTagToString(DwarfTag::kImmutableType));  // Last DWARF 5.

  // Test some out-of-range ones.
  EXPECT_EQ("<undefined (0x4c)>", DwarfTagToString(static_cast<DwarfTag>(0x4c), true));
  EXPECT_EQ("<undefined (0x100)>", DwarfTagToString(static_cast<DwarfTag>(0x100), false));
  EXPECT_EQ("<undefined (0xffff)>", DwarfTagToString(static_cast<DwarfTag>(0xffff), false));
}

}  // namespace zxdb
