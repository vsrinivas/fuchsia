// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/elf_symbol_record.h"

#include "gtest/gtest.h"

namespace zxdb {

TEST(ElfSymbolRecord, Demangle) {
  const char kMangled[] = "_ZN3fxl10LogMessage6streamEv";
  ElfSymbolRecord a(ElfSymbolType::kNormal, 0x1234u, kMangled);
  EXPECT_EQ(0x1234u, a.relative_address);
  EXPECT_EQ(kMangled, a.linkage_name);
  EXPECT_EQ("fxl::LogMessage::stream()", a.unmangled_name);
}

TEST(ElfSymbolRecord, NonMangled) {
  // Given a non-mangled name.
  const char kNonMangled[] = "_FooBar";
  ElfSymbolRecord b(ElfSymbolType::kNormal, 0x5678u, kNonMangled);
  EXPECT_EQ(0x5678u, b.relative_address);
  EXPECT_EQ(kNonMangled, b.linkage_name);
  EXPECT_EQ(kNonMangled, b.unmangled_name);
}

}  // namespace zxdb
