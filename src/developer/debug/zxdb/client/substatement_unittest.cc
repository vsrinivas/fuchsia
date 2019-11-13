// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/substatement.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"

namespace zxdb {

TEST(SubstatementTest, GetSubstatementCallsForMemory) {
  ArchInfo arch;
  Err err = arch.Init(debug_ipc::Arch::kX64);
  ASSERT_FALSE(err.has_error()) << err.msg();

  ProcessSymbolsTestSetup setup;
  setup.InjectMockModule();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);

  constexpr TargetPointer kStartAddr = ProcessSymbolsTestSetup::kDefaultLoadAddress;

  debug_ipc::MemoryBlock dump;
  dump.address = kStartAddr;
  dump.valid = true;
  dump.data = std::vector<uint8_t>{
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx            [inline routine]
      0x48, 0x8d, 0x7c, 0x24, 0x0c,  // lea rdi, [rsp + 0xc]    [code block]
      0xe8, 0xce, 0x00, 0x00, 0x00   // call +0xce (relative to next instruction).
  };
  dump.size = dump.data.size();

  AddressRange abs_extent(kStartAddr, kStartAddr + dump.size);

  // Containing function the current location is inside.
  auto containing_function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  containing_function->set_code_ranges(
      symbol_context.AbsoluteToRelative(AddressRanges({abs_extent})));

  // Inline function that counts as a call.
  auto inline_function = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  constexpr TargetPointer kInlineStart = kStartAddr + 5;
  inline_function->set_code_ranges(symbol_context.AbsoluteToRelative(
      AddressRanges(AddressRange(kInlineStart, kInlineStart + 3))));

  // Lexical scope. This should not count toward the inline calls.
  auto block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  constexpr TargetPointer kBlockStart = kStartAddr + 8;
  block->set_code_ranges(
      AddressRanges(symbol_context.AbsoluteToRelative(AddressRange(kBlockStart, kBlockStart + 5))));

  containing_function->set_inner_blocks({LazySymbol(inline_function), LazySymbol(block)});
  Location loc(kStartAddr, FileLine("file.cc", 21), 0, symbol_context, containing_function);

  std::vector<SubstatementCall> result =
      GetSubstatementCallsForMemory(&arch, &setup.process(), loc, abs_extent, MemoryDump({dump}));
  ASSERT_EQ(2u, result.size());

  EXPECT_EQ(kStartAddr + 5, result[0].call_addr);
  EXPECT_EQ(kStartAddr + 5, result[0].call_dest);
  EXPECT_EQ(inline_function.get(), result[0].inline_call.get());

  EXPECT_EQ(kStartAddr + 0xd, result[1].call_addr);
  EXPECT_EQ(kStartAddr + 0xe0, result[1].call_dest);
  EXPECT_FALSE(result[1].inline_call);
}

}  // namespace zxdb
