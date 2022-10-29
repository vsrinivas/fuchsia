// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_context.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/mock_process.h"
#include "src/developer/debug/zxdb/client/mock_target.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/mock_source_file_provider.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"

namespace zxdb {

static const char kSimpleProgram[] =
    R"(#include "foo.h"

int main(int argc, char** argv) {
  printf("Hello, world");
  return 1;
}
)";

TEST(FormatContext, FormatSourceContext) {
  FormatSourceOpts opts;
  opts.first_line = 2;
  opts.last_line = 6;
  opts.active_line = 4;
  opts.highlight_line = 4;
  opts.highlight_column = 11;

  OutputBuffer out;
  ASSERT_FALSE(FormatSourceContext("file", kSimpleProgram, opts, &out).has_error());
  EXPECT_EQ(
      "   2 \n"
      "   3 int main(int argc, char** argv) {\n"
      " ▶ 4   printf(\"Hello, world\");\n"
      "   5   return 1;\n"
      "   6 }\n",
      out.AsString());
}

TEST(FormatContext, FormatSourceContext_OffBeginning) {
  FormatSourceOpts opts;
  opts.first_line = 0;
  opts.last_line = 4;
  opts.active_line = 2;
  opts.highlight_line = 2;
  opts.highlight_column = 11;

  OutputBuffer out;
  // This column is off the end of line two, and the context has one less line at the beginning
  // because it hit the top of the file.
  ASSERT_FALSE(FormatSourceContext("file", kSimpleProgram, opts, &out).has_error());
  EXPECT_EQ(
      "   1 #include \"foo.h\"\n"
      " ▶ 2 \n"
      "   3 int main(int argc, char** argv) {\n"
      "   4   printf(\"Hello, world\");\n",
      out.AsString());
}

TEST(FormatContext, FormatSourceContext_OffEnd) {
  FormatSourceOpts opts;
  opts.first_line = 4;
  opts.last_line = 8;
  opts.active_line = 6;
  opts.highlight_line = 6;
  opts.highlight_column = 6;

  OutputBuffer out;
  // This column is off the end of line two, and the context has one less line at the beginning
  // because it hit the top of the file.
  ASSERT_FALSE(FormatSourceContext("file", kSimpleProgram, opts, &out).has_error());
  EXPECT_EQ(
      "   4   printf(\"Hello, world\");\n"
      "   5   return 1;\n"
      " ▶ 6 }\n",
      out.AsString());
}

TEST(FormatContext, FormatSourceContext_LineOffEnd) {
  FormatSourceOpts opts;
  opts.first_line = 0;
  opts.last_line = 100;
  opts.active_line = 10;  // This line is off the end of the input.
  opts.highlight_line = 10;
  opts.require_active_line = true;

  OutputBuffer out;
  Err err = FormatSourceContext("file.cc", kSimpleProgram, opts, &out);
  ASSERT_TRUE(err.has_error());
  EXPECT_EQ("There is no line 10 in the file file.cc", err.msg());
}

TEST(FormatContext, FormatAsmContext) {
  ArchInfo arch;
  Err err = arch.Init(debug::Arch::kX64, 4096);
  ASSERT_FALSE(err.has_error());

  // Make a little memory dump.
  constexpr uint64_t start_address = 0x123456780;
  debug_ipc::MemoryBlock block_with_data;
  block_with_data.address = start_address;
  block_with_data.valid = true;
  block_with_data.data = std::vector<uint8_t>{
      0xbf, 0xe0, 0xe5, 0x28, 0x00,  // mov edi, 0x28e5e0
      0x48, 0x89, 0xde,              // mov rsi, rbx
      0x48, 0x8d, 0x7c, 0x24, 0x0c,  // lea rdi, [rsp + 0xc]
      0xe8, 0xce, 0x00, 0x00, 0x00   // call +0xce (relative to next instruction).
  };
  block_with_data.size = static_cast<uint32_t>(block_with_data.data.size());
  MemoryDump dump(std::vector<debug_ipc::MemoryBlock>({block_with_data}));

  FormatAsmOpts opts;
  opts.emit_addresses = true;
  opts.emit_bytes = false;
  opts.active_address = 0x123456785;
  opts.max_instructions = 100;
  opts.include_source = false;
  opts.bp_addrs[start_address] = true;

  OutputBuffer out;
  err = FormatAsmContext(arch, dump, opts, nullptr, SourceFileProvider(), &out);
  ASSERT_FALSE(err.has_error());

  EXPECT_EQ(
      " ◉ 0x123456780  mov   edi, 0x28e5e0 \n"
      " ▶ 0x123456785  mov   rsi, rbx \n"
      "   0x123456788  lea   rdi, [rsp + 0xc] \n"
      "   0x12345678d  call  0xce     ➔ 0x123456860\n",
      out.AsString());

  // Try again with source bytes and a disabled breakpoint on the same line as
  // the active address.
  out = OutputBuffer();
  opts.emit_bytes = true;
  opts.bp_addrs.clear();
  opts.bp_addrs[opts.active_address] = false;
  err = FormatAsmContext(arch, dump, opts, nullptr, SourceFileProvider(), &out);
  ASSERT_FALSE(err.has_error());

  EXPECT_EQ(
      "   0x123456780  bf e0 e5 28 00  mov   edi, 0x28e5e0 \n"
      "◯▶ 0x123456785  48 89 de        mov   rsi, rbx \n"
      "   0x123456788  48 8d 7c 24 0c  lea   rdi, [rsp + 0xc] \n"
      "   0x12345678d  e8 ce 00 00 00  call  0xce     ➔ 0x123456860\n",
      out.AsString());

  // Combined source/assembly.
  out = OutputBuffer();
  opts.emit_bytes = false;
  opts.include_source = true;
  opts.bp_addrs.clear();

  // Source code.
  MockSourceFileProvider file_provider;
  const char kFileName[] = "file.cc";
  file_provider.SetFileData(
      kFileName, SourceFileProvider::FileData("// Copyright\n"                  // Line 1.
                                              "\n"                              // Line 2.
                                              "int main() {\n"                  // Line 3.
                                              "  printf(\"Hello, world.\");\n"  // Line 4.
                                              "  return 0;\n"                   // Line 5.
                                              "}\n",                            // Line 6.
                                              kFileName, 0));

  // Process setup for mocking the symbol requests.
  ProcessSymbolsTestSetup symbols;
  MockModuleSymbols* module_symbols = symbols.InjectMockModule();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);

  Session session;
  MockTarget target(&session);
  MockProcess process(&target);
  process.set_symbols(&symbols.process());

  // Setup address-to-source mapping. These must match the addresses in the assembly. Line 4
  // maps to two addresses.
  module_symbols->AddSymbolLocations(
      0x123456780, {Location(0x123456780, FileLine(kFileName, 4), 0, symbol_context)});
  module_symbols->AddSymbolLocations(
      0x123456785, {Location(0x123456785, FileLine(kFileName, 4), 0, symbol_context)});
  module_symbols->AddSymbolLocations(
      0x123456788, {Location(0x123456788, FileLine(kFileName, 5), 0, symbol_context)});

  err = FormatAsmContext(arch, dump, opts, &process, file_provider, &out);
  ASSERT_FALSE(err.has_error());

  EXPECT_EQ(
      "     1 // Copyright\n"
      "     2 \n"
      "     3 int main() {\n"
      "     4   printf(\"Hello, world.\");\n"
      "   0x123456780  mov   edi, 0x28e5e0 \n"
      " ▶ 0x123456785  mov   rsi, rbx \n"
      "     5   return 0;\n"
      "   0x123456788  lea   rdi, [rsp + 0xc] \n"
      "   0x12345678d  call  0xce     ➔ 0x123456860\n",
      out.AsString());
}

TEST(FormatContext, FormatSourceFileContext_FileName) {
  const char kFileName[] = "file.cc";
  const char kFilePath[] = "/home/me/fuchsia/file.cc";

  MockSourceFileProvider file_provider;
  file_provider.SetFileData(kFileName, SourceFileProvider::FileData("Line 1\n"
                                                                    "Line 2\n"
                                                                    "Line 3\n"
                                                                    "Line 4\n"
                                                                    "Line 5\n",
                                                                    kFilePath, 0));

  FormatSourceOpts opts;
  opts.show_file_name = true;
  opts.first_line = 2;
  opts.last_line = 4;
  opts.active_line = 3;
  opts.highlight_line = 3;
  opts.highlight_column = 0;

  OutputBuffer out;
  Err err = FormatSourceFileContext(FileLine(kFileName, 3), file_provider, opts, &out);
  ASSERT_TRUE(err.ok()) << err.msg();

  EXPECT_EQ(
      "📄 /home/me/fuchsia/file.cc\n"
      "   2 Line 2\n"
      " ▶ 3 Line 3\n"
      "   4 Line 4\n",
      out.AsString());
}

TEST(FormatContext, FormatSourceFileContext_Stale) {
  constexpr std::size_t kFileTime = 10000000;
  const char kFileName[] = "file.cc";
  MockSourceFileProvider file_provider;
  file_provider.SetFileData(kFileName,
                            SourceFileProvider::FileData(kSimpleProgram, kFileName, kFileTime));

  auto mod_sym = fxl::MakeRefCounted<MockModuleSymbols>("file.so");
  // Report build good (module is newer than source file.
  mod_sym->set_modification_time(kFileTime + 10);

  FormatSourceOpts opts;
  opts.first_line = 2;
  opts.last_line = 6;
  opts.active_line = 4;
  opts.highlight_line = 4;
  opts.highlight_column = 11;
  opts.module_for_time_warning = mod_sym->GetWeakPtr();

  std::string expected_code =
      "   2 \n"
      "   3 int main(int argc, char** argv) {\n"
      " ▶ 4   printf(\"Hello, world\");\n"
      "   5   return 1;\n"
      "   6 }\n";

  // Should not give a warning.
  OutputBuffer out;
  ASSERT_FALSE(
      FormatSourceFileContext(FileLine(kFileName, 4), file_provider, opts, &out).has_error());
  EXPECT_EQ(expected_code, out.AsString());

  // Say the module is older. This should give a warning.
  mod_sym->set_modification_time(kFileTime - 10);
  out = OutputBuffer();
  ASSERT_FALSE(
      FormatSourceFileContext(FileLine(kFileName, 4), file_provider, opts, &out).has_error());
  EXPECT_EQ("⚠️  Warning: Source file is newer than the binary. The build may be out-of-date.\n" +
                expected_code,
            out.AsString());

  // Doing the same file again should not give a warning. Each file should be warned about once.
  out = OutputBuffer();
  ASSERT_FALSE(
      FormatSourceFileContext(FileLine(kFileName, 4), file_provider, opts, &out).has_error());
  EXPECT_EQ(expected_code, out.AsString());
}

// Tests line formatting with no syntax highlighting.
TEST(FormatContext, FormatSourceLineNoLang) {
  // Empty line.
  OutputBuffer out = FormatSourceLine(FormatSourceOpts(), false, std::string());
  EXPECT_EQ("kNormal \"\"", out.GetDebugString());

  // Whitespace only.
  out = FormatSourceLine(FormatSourceOpts(), false, "    ");
  EXPECT_EQ("kNormal \"    \"", out.GetDebugString());

  const char kLine[] = "  for (int i = 0; i < a.size(); i++) {";

  // Dim.
  FormatSourceOpts opts;
  opts.dim_others = true;
  out = FormatSourceLine(opts, false, kLine);
  EXPECT_EQ("kComment \"  for (int i = 0; i < a.size(); i++) {\"", out.GetDebugString());

  // Regular.
  opts.dim_others = false;
  out = FormatSourceLine(opts, false, kLine);
  EXPECT_EQ("kNormal \"  for (int i = 0; i < a.size(); i++) {\"", out.GetDebugString());

  // Bold whole line.
  out = FormatSourceLine(opts, true, kLine);
  EXPECT_EQ("kHeading \"  for (int i = 0; i < a.size(); i++) {\"", out.GetDebugString());

  // Bold just part of it.
  opts.highlight_column = 19;  // Bold from "i < a.size()..."
  out = FormatSourceLine(opts, true, kLine);
  EXPECT_EQ("kNormal \"  for (int i = 0; \", kHeading \"i < a.size(); i++) {\"",
            out.GetDebugString());
}

// Tests line formatting with C syntax highlighting.
TEST(FormatContext, FormatSourceLineC) {
  FormatSourceOpts opts;
  opts.language = ExprLanguage::kC;

  // Empty line.
  OutputBuffer out = FormatSourceLine(opts, false, std::string());
  EXPECT_EQ("kNormal \"\"", out.GetDebugString());

  // Whitespace only.
  out = FormatSourceLine(opts, false, "    ");
  EXPECT_EQ("kNormal \"    \"", out.GetDebugString());

  // Tokenization error should fall back to unformatted.
  out = FormatSourceLine(opts, false, " hello 'world");
  EXPECT_EQ("kNormal \" hello 'world\"", out.GetDebugString());

  const char kLine[] = "  for (int i = 0; i < a.size(); i++) {";

  // Dim.
  opts.dim_others = true;
  out = FormatSourceLine(opts, false, kLine);
  EXPECT_EQ(
      "kComment \"  \", kKeywordDim \"for \", kOperatorDim \"(\", kKeywordDim \"int \", "
      "kComment \"i \", kOperatorDim \"= \", kNumberDim \"0\", kOperatorDim \"; \", "
      "kComment \"i \", kOperatorDim \"< \", kComment \"a\", kOperatorDim \".\", "
      "kComment \"size\", kOperatorDim \"(); \", kComment \"i\", kOperatorDim \"++) {\"",
      out.GetDebugString());

  // Regular.
  opts.dim_others = false;
  out = FormatSourceLine(opts, false, kLine);
  EXPECT_EQ(
      "kNormal \"  \", kKeywordNormal \"for \", kOperatorNormal \"(\", kKeywordNormal \"int \", "
      "kNormal \"i \", kOperatorNormal \"= \", kNumberNormal \"0\", kOperatorNormal \"; \", "
      "kNormal \"i \", kOperatorNormal \"< \", kNormal \"a\", kOperatorNormal \".\", "
      "kNormal \"size\", kOperatorNormal \"(); \", kNormal \"i\", kOperatorNormal \"++) {\"",
      out.GetDebugString());

  // Bold whole line.
  out = FormatSourceLine(opts, true, kLine);
  EXPECT_EQ(
      "kHeading \"  \", kKeywordBold \"for \", kOperatorBold \"(\", kKeywordBold \"int \", "
      "kHeading \"i \", kOperatorBold \"= \", kNumberBold \"0\", kOperatorBold \"; \", "
      "kHeading \"i \", kOperatorBold \"< \", kHeading \"a\", kOperatorBold \".\", "
      "kHeading \"size\", kOperatorBold \"(); \", kHeading \"i\", kOperatorBold \"++) {\"",
      out.GetDebugString());

  // Bold just part of it.
  opts.highlight_column = 19;  // Bold from "i < a.size()..."
  out = FormatSourceLine(opts, true, kLine);
  EXPECT_EQ(
      "kNormal \"  \", kKeywordNormal \"for \", kOperatorNormal \"(\", kKeywordNormal \"int \", "
      "kNormal \"i \", kOperatorNormal \"= \", kNumberNormal \"0\", kOperatorNormal \"; \", "
      "kHeading \"i \", kOperatorBold \"< \", kHeading \"a\", kOperatorBold \".\", "
      "kHeading \"size\", kOperatorBold \"(); \", kHeading \"i\", kOperatorBold \"++) {\"",
      out.GetDebugString());
}

TEST(FormatContext, FormatSourceLineComment) {
  FormatSourceOpts opts;
  opts.language = ExprLanguage::kC;

  // Block comment.
  OutputBuffer out = FormatSourceLine(opts, false, "1 /* foo */ 2");
  EXPECT_EQ("kNumberNormal \"1 \", kComment \"/* foo */ \", kNumberNormal \"2\"",
            out.GetDebugString());

  // End-of-line comment.
  out = FormatSourceLine(opts, false, "1 // foo bar");
  EXPECT_EQ("kNumberNormal \"1 \", kComment \"// foo bar\"", out.GetDebugString());

  // Unterminated block comment.
  out = FormatSourceLine(opts, false, "1 /* foo");
  EXPECT_EQ("kNumberNormal \"1 \", kComment \"/* foo\"", out.GetDebugString());

  // Block comment with no begin (will happen if the block starts on a previous line).
  out = FormatSourceLine(opts, false, "foo */ 2");
  EXPECT_EQ("kComment \"foo */ \", kNumberNormal \"2\"", out.GetDebugString());
}

TEST(FormatContext, FormatSourceLineRust) {
  FormatSourceOpts opts;
  opts.language = ExprLanguage::kRust;

  // Empty line.
  OutputBuffer out = FormatSourceLine(opts, false, std::string());
  EXPECT_EQ("kNormal \"\"", out.GetDebugString());

  // Whitespace only.
  out = FormatSourceLine(opts, false, "    ");
  EXPECT_EQ("kNormal \"    \"", out.GetDebugString());

  // Tokenization error should fall back to unformatted.
  out = FormatSourceLine(opts, false, " hello \"world");
  EXPECT_EQ("kNormal \" hello \"world\"", out.GetDebugString());

  const char kLine[] = "let temp_dir = TempDir::new_in(\"cache\")";

  // Regular.
  out = FormatSourceLine(opts, false, kLine);
  EXPECT_EQ(
      "kKeywordNormal \"let \", kNormal \"temp_dir \", kOperatorNormal \"= \", "
      "kNormal \"TempDir\", kOperatorNormal \"::\", kNormal \"new_in\", kOperatorNormal \"(\", "
      "kStringNormal \"\"cache\"\", kOperatorNormal \")\"",
      out.GetDebugString());
}

// C preprocessor and Rust attributes.
TEST(FormatContext, Octothorpe) {
  FormatSourceOpts rust_opts;
  rust_opts.language = ExprLanguage::kRust;
  OutputBuffer out = FormatSourceLine(rust_opts, false, "  #[cfg(target_os = \"fuchsia\")]");
  EXPECT_EQ("kComment \"  #[cfg(target_os = \"fuchsia\")]\"", out.GetDebugString());

  FormatSourceOpts c_opts;
  c_opts.language = ExprLanguage::kC;
  out = FormatSourceLine(c_opts, false, "#include <stdio.h>");
  EXPECT_EQ("kComment \"#include <stdio.h>\"", out.GetDebugString());
}

}  // namespace zxdb
