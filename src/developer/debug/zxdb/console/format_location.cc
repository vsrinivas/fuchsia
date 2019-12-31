// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_location.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

FormatLocationOptions::FormatLocationOptions(const Target* target) : FormatLocationOptions() {
  if (target) {
    show_file_path =
        target->session()->system().settings().GetBool(ClientSettings::System::kShowFilePaths);
    target_symbols = target->GetSymbols();
  }
}

OutputBuffer FormatLocation(const Location& loc, const FormatLocationOptions& opts) {
  if (!loc.is_valid())
    return OutputBuffer("<invalid address>");
  if (!loc.has_symbols())
    return OutputBuffer(fxl::StringPrintf("0x%" PRIx64, loc.address()));

  OutputBuffer result;
  if (opts.always_show_addresses) {
    result = OutputBuffer(Syntax::kComment, fxl::StringPrintf("0x%" PRIx64 ", ", loc.address()));
  }

  bool show_file_line = opts.show_file_line && loc.file_line().is_valid();

  const Symbol* symbol = loc.symbol().Get();
  if (const Function* func = symbol->AsFunction()) {
    // Regular function.
    OutputBuffer func_output = FormatFunctionName(func, opts.func);
    if (!func_output.empty()) {
      result.Append(std::move(func_output));
      if (show_file_line) {
        // Separator between function and file/line.
        result.Append(" " + GetBullet() + " ");
      } else {
        // Check if the address is inside a function and show the offset.
        AddressRange function_range = func->GetFullRange(loc.symbol_context());
        if (function_range.InRange(loc.address())) {
          // Inside a function but no file/line known. Show the offset.
          uint64_t offset = loc.address() - function_range.begin();
          if (offset)
            result.Append(fxl::StringPrintf(" + 0x%" PRIx64, offset));
          if (opts.show_file_line)
            result.Append(Syntax::kComment, " (no line info)");
        }
      }
    }
  } else if (const ElfSymbol* elf_symbol = symbol->AsElfSymbol()) {
    // ELF symbol.
    FormatIdentifierOptions opts;
    opts.show_global_qual = false;
    opts.bold_last = true;
    result.Append(FormatIdentifier(symbol->GetIdentifier(), opts));

    // The address might not be at the beginning of the symbol.
    if (uint64_t offset =
            loc.address() - loc.symbol_context().RelativeToAbsolute(elf_symbol->relative_address()))
      result.Append(fxl::StringPrintf(" + 0x%" PRIx64, offset));
  } else {
    // All other symbol types. This case must handle all other symbol types, some of which might
    // not have identifiers.
    bool printed_name = false;
    if (!symbol->GetIdentifier().empty()) {
      FormatIdentifierOptions opts;
      opts.show_global_qual = false;
      opts.bold_last = true;
      result.Append(FormatIdentifier(symbol->GetIdentifier(), opts));
      printed_name = true;
    } else if (!symbol->GetFullName().empty()) {
      // Fall back on the name.
      result.Append(symbol->GetFullName());
      printed_name = true;
    } else if (!opts.always_show_addresses) {
      // Unnamed symbol, use the address (unless it was printed above already).
      result.Append(fxl::StringPrintf("0x%" PRIx64, loc.address()));
      printed_name = true;
    }

    // Separator between function and file/line.
    if (printed_name && show_file_line)
      result.Append(" " + GetBullet() + " ");
  }

  if (show_file_line) {
    // Showing the file path means not passing the target symbols because the target symbols is
    // used to shorten the paths.
    result.Append(
        DescribeFileLine(opts.show_file_path ? nullptr : opts.target_symbols, loc.file_line()));
  }
  return result;
}

std::string DescribeFileLine(const TargetSymbols* optional_target_symbols,
                             const FileLine& file_line) {
  std::string result;

  // Name.
  if (file_line.file().empty()) {
    result = "?";
  } else if (!optional_target_symbols) {
    result = file_line.file();
  } else {
    result = optional_target_symbols->GetShortestUniqueFileName(file_line.file());
  }

  result.push_back(':');

  // Line.
  if (file_line.line() == 0)
    result.push_back('?');
  else
    result.append(fxl::StringPrintf("%d", file_line.line()));

  return result;
}

}  // namespace zxdb
