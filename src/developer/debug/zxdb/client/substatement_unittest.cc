// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/substatement.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"

namespace zxdb {

namespace {

class SubstatementTest : public RemoteAPITest {};

}  // namespace

// Several instructions on a single line with no inlines bit with some "line 0" line table entries
// in the middle. The "line 0" instructions should be skipped and all matching line table entries
// should be attributed to the line.
TEST_F(SubstatementTest, GetSubstatementCallsForLine_Line0) {
  constexpr uint64_t kProcessKoid = 1234;
  constexpr uint64_t kLoadAddress = 0x1000000;
  Process* process = InjectProcess(kProcessKoid);
  auto mock_module_symbols = InjectMockModule(process, kLoadAddress);
  SymbolContext symbol_context(kLoadAddress);

  std::vector<uint8_t> data{
      0xe8, 0xce, 0x00, 0x00, 0x00,  // call +0xce (relative to next instruction).  [line 21]
      0x48, 0x89, 0xde,              // mov rsi, rbx                                [line 0]
      0x48, 0x8d, 0x7c, 0x24, 0x0c,  // lea rdi, [rsp + 0xc]                        [line 21]
      0xff, 0xd0,                    // call rax                                    [line 21]
      0x48, 0x89, 0xde,              // mov rsi, rbx                                [line 22]
  };
  mock_remote_api()->AddMemory(kLoadAddress, data);

  // Ranges covering the addresses for each instruction in the data above.
  const AddressRange kInstrAddressRanges[5] = {AddressRange(kLoadAddress, kLoadAddress + 5),
                                               AddressRange(kLoadAddress + 5, kLoadAddress + 8),
                                               AddressRange(kLoadAddress + 8, kLoadAddress + 13),
                                               AddressRange(kLoadAddress + 13, kLoadAddress + 15),
                                               AddressRange(kLoadAddress + 15, kLoadAddress + 18)};

  FileLine source_file_line("file.cc", 21);
  FileLine next_file_line("file.cc", 22);
  FileLine zero_file_line;

  // In this scheme, each instruction has its own line table entry.
  mock_module_symbols->AddLineDetails(
      kInstrAddressRanges[0].begin(),
      LineDetails(source_file_line, {LineDetails::LineEntry(kInstrAddressRanges[0])}));
  mock_module_symbols->AddLineDetails(
      kInstrAddressRanges[1].begin(),
      LineDetails(zero_file_line, {LineDetails::LineEntry(kInstrAddressRanges[1])}));
  mock_module_symbols->AddLineDetails(
      kInstrAddressRanges[2].begin(),
      LineDetails(source_file_line, {LineDetails::LineEntry(kInstrAddressRanges[2])}));
  mock_module_symbols->AddLineDetails(
      kInstrAddressRanges[3].begin(),
      LineDetails(source_file_line, {LineDetails::LineEntry(kInstrAddressRanges[3])}));
  mock_module_symbols->AddLineDetails(
      kInstrAddressRanges[4].begin(),
      LineDetails(next_file_line, {LineDetails::LineEntry(kInstrAddressRanges[4])}));

  AddressRange abs_extent(kLoadAddress, kLoadAddress + data.size());

  // Containing function the current location is inside.
  auto containing_function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  containing_function->set_code_ranges(
      symbol_context.AbsoluteToRelative(AddressRanges({abs_extent})));
  Location loc(kLoadAddress, source_file_line, 0, symbol_context, containing_function);

  std::vector<SubstatementCall> result;
  GetSubstatementCallsForLine(process, loc,
                              [&result](const Err& err, std::vector<SubstatementCall> in_result) {
                                EXPECT_FALSE(err.has_error());
                                result = std::move(in_result);
                              });
  EXPECT_TRUE(result.empty());  // Expect the callback to be run asynchronously.
  loop().RunUntilNoTasks();

  ASSERT_EQ(2u, result.size());

  // First call is direct.
  EXPECT_EQ(kInstrAddressRanges[0].begin(), result[0].call_addr);
  ASSERT_TRUE(result[0].call_dest);
  EXPECT_EQ(kInstrAddressRanges[1].begin() + 0xce, *result[0].call_dest);
  EXPECT_FALSE(result[0].inline_call);

  // 2nd call is indirect.
  EXPECT_EQ(kInstrAddressRanges[3].begin(), result[1].call_addr);
  EXPECT_FALSE(result[1].call_dest);  // No known destintation for indirect call.
  EXPECT_FALSE(result[1].inline_call);
}

TEST_F(SubstatementTest, GetSubstatementCallsForLine_WithInlines) {
  constexpr uint64_t kProcessKoid = 1234;
  constexpr uint64_t kLoadAddress = 0x1000000;
  Process* process = InjectProcess(kProcessKoid);
  auto mock_module_symbols = InjectMockModule(process, kLoadAddress);
  SymbolContext symbol_context(kLoadAddress);

  std::vector<uint8_t> data{
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx            [inline routine]
      0x48, 0x8d, 0x7c, 0x24, 0x0c,  // lea rdi, [rsp + 0xc]    [code block]
      0xe8, 0xce, 0x00, 0x00, 0x00,  // call +0xce (relative to next instruction).
      0xe8, 0xd0, 0x00, 0x00, 0x00   // call +0xd0 (relative to next instruction).
  };
  mock_remote_api()->AddMemory(kLoadAddress, data);

  // Ranges covering the addresses for each instruction in the data above.
  const AddressRange kInstrAddressRanges[5] = {AddressRange(kLoadAddress, kLoadAddress + 5),
                                               AddressRange(kLoadAddress + 5, kLoadAddress + 8),
                                               AddressRange(kLoadAddress + 8, kLoadAddress + 13),
                                               AddressRange(kLoadAddress + 13, kLoadAddress + 18),
                                               AddressRange(kLoadAddress + 18, kLoadAddress + 23)};

  FileLine source_file_line("file.cc", 21);

  // Line information for the first instruction.
  mock_module_symbols->AddLineDetails(
      kInstrAddressRanges[0].begin(),
      LineDetails(source_file_line, {LineDetails::LineEntry(kInstrAddressRanges[0])}));
  // The second instruction is from some other file that was inlined.
  mock_module_symbols->AddLineDetails(
      kInstrAddressRanges[1].begin(),
      LineDetails(FileLine("foo.h", 12), {LineDetails::LineEntry(kInstrAddressRanges[1])}));
  // The third instruction's line entry covers the 3rd and 4th instructions.
  mock_module_symbols->AddLineDetails(
      kInstrAddressRanges[2].begin(),
      LineDetails(source_file_line,
                  {LineDetails::LineEntry(AddressRange(kInstrAddressRanges[2].begin(),
                                                       kInstrAddressRanges[3].end()))}));
  // The fifth instruction is on the next line.
  mock_module_symbols->AddLineDetails(
      kInstrAddressRanges[4].begin(),
      LineDetails(FileLine(source_file_line.file(), source_file_line.line() + 1),
                  {LineDetails::LineEntry(kInstrAddressRanges[4])}));

  AddressRange abs_extent(kLoadAddress, kLoadAddress + data.size());

  // Containing function the current location is inside.
  auto containing_function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  containing_function->set_code_ranges(
      symbol_context.AbsoluteToRelative(AddressRanges({abs_extent})));

  // Inline function that counts as a call.
  auto inline_function = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  constexpr TargetPointer kInlineStart = kLoadAddress + 5;
  inline_function->set_code_ranges(symbol_context.AbsoluteToRelative(
      AddressRanges(AddressRange(kInlineStart, kInlineStart + 3))));
  inline_function->set_call_line(source_file_line);

  // Lexical scope. This should not count toward the inline calls.
  auto block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  constexpr TargetPointer kBlockStart = kLoadAddress + 8;
  block->set_code_ranges(
      AddressRanges(symbol_context.AbsoluteToRelative(AddressRange(kBlockStart, kBlockStart + 5))));

  containing_function->set_inner_blocks({LazySymbol(inline_function), LazySymbol(block)});
  Location loc(kLoadAddress, source_file_line, 0, symbol_context, containing_function);

  std::vector<SubstatementCall> result;
  GetSubstatementCallsForLine(process, loc,
                              [&result](const Err& err, std::vector<SubstatementCall> in_result) {
                                EXPECT_FALSE(err.has_error());
                                result = std::move(in_result);
                              });
  EXPECT_TRUE(result.empty());  // Expect the callback to be run asynchronously.

  loop().RunUntilNoTasks();
  ASSERT_EQ(2u, result.size());

  // Inline call.
  EXPECT_EQ(kInstrAddressRanges[1].begin(), result[0].call_addr);
  EXPECT_EQ(kInstrAddressRanges[1].begin(), result[0].call_dest);
  EXPECT_EQ(inline_function.get(), result[0].inline_call.get());

  // Physical call is the 4th instruction.
  EXPECT_EQ(kInstrAddressRanges[3].begin(), result[1].call_addr);
  EXPECT_EQ(kLoadAddress + 0xe0, result[1].call_dest);
  EXPECT_FALSE(result[1].inline_call);
}

}  // namespace zxdb
