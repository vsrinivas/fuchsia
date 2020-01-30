// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/elf_symbol.h"

#include "gtest/gtest.h"

namespace zxdb {

// Tests the name getting for the full unmangled symbols.
TEST(ElfSymbol, Names) {
  const uint64_t kAddress = 0x1cfcc0;
  const char kMangled[] =
      "_ZZZN11debug_agent16SocketConnection6AcceptEPN9debug_ipc11MessageLoopEiEN3$_0clEvENKUlvE0_"
      "clEv";
  ElfSymbolRecord record(ElfSymbolType::kNormal, kAddress, 0, kMangled);
  auto elf_symbol = fxl::MakeRefCounted<ElfSymbol>(fxl::WeakPtr<ModuleSymbols>(), record);

  EXPECT_EQ(kAddress, elf_symbol->relative_address());
  EXPECT_EQ(kMangled, elf_symbol->linkage_name());

  const char kUnmangled[] =
      "debug_agent::SocketConnection::Accept(debug_ipc::MessageLoop*, "
      "int)::$_0::operator()()::'lambda0'()::operator()() const";
  EXPECT_EQ(kUnmangled, elf_symbol->GetFullName());

  // Currently everything is stuffed into one identifier component.
  // TODO(bug 41928) at least fix this for function calls. This will likely always be the case for
  // vtable pointers, etc.
  Identifier ident = elf_symbol->GetIdentifier();
  ASSERT_EQ(1u, ident.components().size());
  EXPECT_EQ(kUnmangled, ident.components()[0].name());
}

}  // namespace zxdb
