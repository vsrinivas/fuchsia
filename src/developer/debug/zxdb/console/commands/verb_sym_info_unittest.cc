// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_sym_info.h"

#include <gtest/gtest.h>

#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/console/console_test.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/mock_source_file_provider.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

namespace {

class VerbSymInfo : public ConsoleTest {};

}  // namespace

// sym-info will demangle symbols. It doesn't need any context to do this.
TEST_F(VerbSymInfo, SymInfo_Demangle) {
  // Should demangle if given mangled input.
  console().ProcessInputLine("sym-info _ZN3fxl10LogMessage6streamEv");
  auto event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "Demangled name: fxl::LogMessage::stream()\n\n"
      "No symbol \"_ZN3fxl10LogMessage6streamEv\" found in the current context.\n",
      event.output.AsString());

  // When input is not mangled it shouldn't show any demangled thing.
  console().ProcessInputLine("sym-info LogMessage6streamEv");
  event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("No symbol \"LogMessage6streamEv\" found in the current context.\n",
            event.output.AsString());

  // Shouldn't demangle basic types. "i" would normally be converted to "int" by the demangler.
  console().ProcessInputLine("sym-info i");
  event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("No symbol \"i\" found in the current context.\n", event.output.AsString());
}

// Tests ELF PLT symbol printing.
TEST_F(VerbSymInfo, PLTFunctionInfo) {
  constexpr uint64_t kLoadAddr = 0x100000;
  SymbolContext symbol_context(kLoadAddr);
  auto mock_module = InjectMockModule(process(), kLoadAddr);

  Identifier plt_identifier;
  plt_identifier.AppendComponent(IdentifierComponent(SpecialIdentifier::kPlt, "zx_channel_write"));

  // ELF symbol for the PLT identifier.
  constexpr uint64_t kElfRelativeAddress = 0x500;
  ElfSymbolRecord plt_record(ElfSymbolType::kPlt, kElfRelativeAddress, 8, "zx_channel_write");
  auto plt_symbol = fxl::MakeRefCounted<ElfSymbol>(mock_module->GetWeakPtr(), plt_record);

  Location plt_location(kLoadAddr + kElfRelativeAddress, FileLine(), 0, symbol_context, plt_symbol);
  mock_module->AddSymbolLocations(plt_identifier, {plt_location});

  console().ProcessInputLine("sym-info $plt(zx_channel_write)");
  auto event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "ELF PLT symbol: zx_channel_write\n"
      "  Address: 0x100500\n"
      "  Size: 0x8\n"
      "\n",
      event.output.AsString());
}

}  // namespace zxdb
