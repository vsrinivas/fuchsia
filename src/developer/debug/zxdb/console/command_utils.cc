// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_utils.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>

#include <limits>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_location.h"
#include "src/developer/debug/zxdb/client/client_eval_context_impl.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/job_context.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_context.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/number_parser.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol_utils.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/trim.h"

namespace zxdb {

Err AssertRunningTarget(ConsoleContext* context, const char* command_name, Target* target) {
  Target::State state = target->GetState();
  if (state == Target::State::kRunning)
    return Err();
  return Err(ErrType::kInput,
             fxl::StringPrintf("%s requires a running process but process %d is %s.", command_name,
                               context->IdForTarget(target), TargetStateToString(state)));
}

Err AssertStoppedThreadCommand(ConsoleContext* context, const Command& cmd, bool validate_nouns,
                               const char* command_name) {
  Err err;
  if (validate_nouns) {
    err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
    if (err.has_error())
      return err;
  }

  if (!cmd.thread()) {
    return Err("\"%s\" requires a thread but there is no current thread.", command_name);
  }
  if (cmd.thread()->GetState() != debug_ipc::ThreadRecord::State::kBlocked &&
      cmd.thread()->GetState() != debug_ipc::ThreadRecord::State::kCoreDump &&
      cmd.thread()->GetState() != debug_ipc::ThreadRecord::State::kSuspended) {
    return Err(
        "\"%s\" requires a suspended thread but thread %d is %s.\n"
        "To view and sync thread state with the remote system, type "
        "\"thread\".",
        command_name, context->IdForThread(cmd.thread()),
        ThreadStateToString(cmd.thread()->GetState(), cmd.thread()->GetBlockedReason()).c_str());
  }
  return Err();
}

Err AssertStoppedThreadWithFrameCommand(ConsoleContext* context, const Command& cmd,
                                        const char* command_name) {
  // Does most validation except noun checking.
  Err err = AssertStoppedThreadCommand(context, cmd, false, command_name);
  if (err.has_error())
    return err;

  if (!cmd.frame()) {
    return Err(
        "\"%s\" requires a stack frame but none is available.\n"
        "You may need to \"pause\" the thread or sync the frames with \"frame\".",
        command_name);
  }

  return cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
}

size_t CheckHexPrefix(const std::string& s) {
  if (s.size() >= 2u && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    return 2u;
  return 0u;
}

Err StringToInt(const std::string& s, int* out) {
  int64_t value64;
  Err err = StringToInt64(s, &value64);
  if (err.has_error())
    return err;

  // Range check it can be stored in an int.
  if (value64 < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
      value64 > static_cast<int64_t>(std::numeric_limits<int>::max()))
    return Err("This value is too large for an integer.");

  *out = static_cast<int>(value64);
  return Err();
}

Err StringToInt64(const std::string& s, int64_t* out) {
  *out = 0;

  // StringToNumber expects pre-trimmed input.
  std::string trimmed = fxl::TrimString(s, " ").ToString();

  ErrOrValue number_value = StringToNumber(trimmed);
  if (number_value.has_error())
    return number_value.err();

  // Be careful to read the number out in its original sign-edness.
  if (number_value.value().GetBaseType() == BaseType::kBaseTypeUnsigned) {
    uint64_t u64;
    Err err = number_value.value().PromoteTo64(&u64);
    if (err.has_error())
      return err;

    // Range-check that the unsigned value can be put in a signed.
    if (u64 > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return Err("This value is too large.");
    *out = static_cast<int64_t>(u64);
    return Err();
  }

  // Expect everything else to be a signed number.
  if (number_value.value().GetBaseType() != BaseType::kBaseTypeSigned)
    return Err("This value is not the correct type.");
  return number_value.value().PromoteTo64(out);
}

Err StringToUint32(const std::string& s, uint32_t* out) {
  // Re-uses StringToUint64's and just size-checks the output.
  uint64_t value64;
  Err err = StringToUint64(s, &value64);
  if (err.has_error())
    return err;

  if (value64 > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    return Err("Expected 32-bit unsigned value, but %s is too large.", s.c_str());
  }
  *out = static_cast<uint32_t>(value64);
  return Err();
}

Err StringToUint64(const std::string& s, uint64_t* out) {
  *out = 0;

  // StringToNumber expects pre-trimmed input.
  std::string trimmed = fxl::TrimString(s, " ").ToString();

  ErrOrValue number_value = StringToNumber(trimmed);
  if (number_value.has_error())
    return number_value.err();

  // Be careful to read the number out in its original sign-edness.
  if (number_value.value().GetBaseType() == BaseType::kBaseTypeSigned) {
    int64_t s64;
    Err err = number_value.value().PromoteTo64(&s64);
    if (err.has_error())
      return err;

    // Range-check that the signed value can be put in an unsigned.
    if (s64 < 0)
      return Err("This value can not be negative.");
    *out = static_cast<uint64_t>(s64);
    return Err();
  }

  // Expect everything else to be an unsigned number.
  if (number_value.value().GetBaseType() != BaseType::kBaseTypeUnsigned)
    return Err("This value is not the correct type.");
  return number_value.value().PromoteTo64(out);
}

Err ReadUint64Arg(const Command& cmd, size_t arg_index, const char* param_desc, uint64_t* out) {
  if (cmd.args().size() <= arg_index) {
    return Err(ErrType::kInput,
               fxl::StringPrintf("Not enough arguments when reading the %s.", param_desc));
  }
  Err result = StringToUint64(cmd.args()[arg_index], out);
  if (result.has_error()) {
    return Err(ErrType::kInput, fxl::StringPrintf("Invalid number \"%s\" when reading the %s.",
                                                  cmd.args()[arg_index].c_str(), param_desc));
  }
  return Err();
}

std::string ThreadStateToString(debug_ipc::ThreadRecord::State state,
                                debug_ipc::ThreadRecord::BlockedReason blocked_reason) {
  // Blocked can have many cases, so we handle it separately.
  if (state != debug_ipc::ThreadRecord::State::kBlocked)
    return debug_ipc::ThreadRecord::StateToString(state);

  FXL_DCHECK(blocked_reason != debug_ipc::ThreadRecord::BlockedReason::kNotBlocked)
      << "A blocked thread has to have a valid reason.";
  return fxl::StringPrintf("Blocked (%s)",
                           debug_ipc::ThreadRecord::BlockedReasonToString(blocked_reason));
}

std::string ExecutionScopeToString(const ConsoleContext* context, const ExecutionScope& scope) {
  switch (scope.type()) {
    case ExecutionScope::kSystem:
      return "Global";
    case ExecutionScope::kTarget:
      if (scope.target())
        return fxl::StringPrintf("pr %d", context->IdForTarget(scope.target()));
      return "<Deleted process>";
    case ExecutionScope::kThread:
      if (scope.thread()) {
        return fxl::StringPrintf("pr %d t %d", context->IdForTarget(scope.target()),
                                 context->IdForThread(scope.thread()));
      }
      return "<Deleted thread>";
  }
  FXL_NOTREACHED();
  return std::string();
}

ExecutionScope ExecutionScopeForCommand(const Command& cmd) {
  if (cmd.HasNoun(Noun::kThread))
    return ExecutionScope(cmd.thread());  // Thread context given explicitly.
  if (cmd.HasNoun(Noun::kProcess))
    return ExecutionScope(cmd.target());  // Target context given explicitly.

  return ExecutionScope();  // Everything else becomes global scope.
}

std::string BreakpointStopToString(BreakpointSettings::StopMode mode) {
  switch (mode) {
    case BreakpointSettings::StopMode::kNone:
      return "None";
    case BreakpointSettings::StopMode::kThread:
      return "Thread";
    case BreakpointSettings::StopMode::kProcess:
      return "Process";
    case BreakpointSettings::StopMode::kAll:
      return "All";
  }
  FXL_NOTREACHED();
  return std::string();
}

const char* BreakpointEnabledToString(bool enabled) { return enabled ? "Enabled" : "Disabled"; }

std::string DescribeThread(const ConsoleContext* context, const Thread* thread) {
  return fxl::StringPrintf(
      "Thread %d [%s] koid=%" PRIu64 " %s", context->IdForThread(thread),
      ThreadStateToString(thread->GetState(), thread->GetBlockedReason()).c_str(),
      thread->GetKoid(), thread->GetName().c_str());
}

OutputBuffer FormatBreakpoint(const ConsoleContext* context, const Breakpoint* breakpoint,
                              bool show_context) {
  BreakpointSettings settings = breakpoint->GetSettings();

  std::string scope = ExecutionScopeToString(context, settings.scope);
  std::string stop = BreakpointStopToString(settings.stop_mode);
  const char* enabled = BreakpointEnabledToString(settings.enabled);
  const char* type = BreakpointTypeToString(settings.type);
  OutputBuffer location = FormatInputLocations(settings.locations);

  size_t matched_locs = breakpoint->GetLocations().size();
  const char* locations_str = matched_locs == 1 ? "addr" : "addrs";

  OutputBuffer result("Breakpoint ");
  result.Append(Syntax::kSpecial, fxl::StringPrintf("%d", context->IdForBreakpoint(breakpoint)));
  result.Append(fxl::StringPrintf(" (%s) on %s, %s, Stop %s, %zu %s @ ", type, scope.c_str(),
                                  enabled, stop.c_str(), matched_locs, locations_str));

  result.Append(std::move(location));
  result.Append("\n");

  if (!settings.locations.empty() && show_context) {
    // Append the source code location.
    //
    // There is a question of how to show the breakpoint enabled state. The breakpoint has a main
    // enabled bit and each location (it can apply to more than one address -- think templates and
    // inlined functions) within that breakpoint has its own. But each location normally resolves to
    // the same source code location so we can't practically show the individual location's enabled
    // state separately.
    //
    // For simplicity, just base it on the main enabled bit. Most people won't use location-specific
    // enabling anyway.
    //
    // Ignore errors from printing the source, it doesn't matter that much. Since breakpoints are in
    // the global scope we have to use the global settings for the build dir. We could use the
    // process build dir for process-specific breakpoints but both process-specific breakpoints and
    // process-specific build settings are rare.
    if (auto locs = breakpoint->GetLocations(); !locs.empty()) {
      FormatBreakpointContext(locs[0]->GetLocation(),
                              SourceFileProviderImpl(breakpoint->session()->system().settings()),
                              settings.enabled, &result);
    } else {
      // When the breakpoint resolved to nothing, warn the user, they may have made a typo.
      result.Append(Syntax::kWarning, "Pending");
      result.Append(": No matches for location, it will be pending library loads.\n");
    }
  }
  return result;
}

OutputBuffer FormatInputLocation(const InputLocation& location) {
  switch (location.type) {
    case InputLocation::Type::kNone: {
      return OutputBuffer(Syntax::kComment, "<no location>");
    }
    case InputLocation::Type::kLine: {
      // Don't pass a TargetSymbols to DescribeFileLine because we always want
      // the full file name as passed-in by the user (as this is an "input"
      // location object). It is surprising if the debugger deletes some input.
      return OutputBuffer(DescribeFileLine(nullptr, location.line));
    }
    case InputLocation::Type::kName: {
      FormatIdentifierOptions opts;
      opts.show_global_qual = true;  // Imporant to disambiguate for InputLocations.
      opts.bold_last = true;
      return FormatIdentifier(location.name, opts);
    }
    case InputLocation::Type::kAddress: {
      return OutputBuffer(fxl::StringPrintf("0x%" PRIx64, location.address));
    }
  }
  FXL_NOTREACHED();
  return OutputBuffer();
}

OutputBuffer FormatInputLocations(const std::vector<InputLocation>& locations) {
  if (locations.empty())
    return OutputBuffer(Syntax::kComment, "<no location>");

  // Comma-separate if there are multiples.
  bool first_location = true;
  OutputBuffer result;
  for (const auto& loc : locations) {
    if (!first_location)
      result.Append(", ");
    else
      first_location = false;
    result.Append(FormatInputLocation(loc));
  }
  return result;
}

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

fxl::RefPtr<EvalContext> GetEvalContextForCommand(const Command& cmd) {
  if (cmd.frame())
    return cmd.frame()->GetEvalContext();

  std::optional<ExprLanguage> language;
  auto language_setting =
      cmd.target()->session()->system().settings().GetString(ClientSettings::System::kLanguage);
  if (language_setting == ClientSettings::System::kLanguage_Rust) {
    language = ExprLanguage::kRust;
  } else if (language_setting == ClientSettings::System::kLanguage_Cpp) {
    language = ExprLanguage::kC;
  } else {
    FXL_DCHECK(language_setting == ClientSettings::System::kLanguage_Auto);
  }

  // Target context only (it may or may not have a process).
  return fxl::MakeRefCounted<ClientEvalContextImpl>(cmd.target(), language);
}

Err EvalCommandExpression(const Command& cmd, const char* verb,
                          fxl::RefPtr<EvalContext> eval_context, bool follow_references,
                          EvalCallback cb) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;

  if (cmd.args().size() != 1)
    return Err("Usage: %s <expression>\nSee \"help %s\" for more.", verb, verb);

  EvalExpression(cmd.args()[0], std::move(eval_context), follow_references, std::move(cb));
  return Err();
}

Err EvalCommandAddressExpression(
    const Command& cmd, const char* verb, fxl::RefPtr<EvalContext> eval_context,
    fit::callback<void(const Err& err, uint64_t address, std::optional<uint32_t> size)> cb) {
  return EvalCommandExpression(
      cmd, verb, eval_context, true, [eval_context, cb = std::move(cb)](ErrOrValue value) mutable {
        if (value.has_error())
          return cb(value.err(), 0, std::nullopt);

        fxl::RefPtr<Type> concrete_type = value.value().GetConcreteType(eval_context.get());
        if (concrete_type->AsCollection()) {
          // Don't allow structs and classes that are <= 64 bits to be converted
          // to addresses.
          return cb(Err("Can't convert '%s' to an address.", concrete_type->GetFullName().c_str()),
                    0, std::nullopt);
        }

        // See if there's an intrinsic size to the object being pointed to. This
        // is true for pointers. References should have been followed and
        // stripped before here.
        std::optional<uint32_t> size;
        if (auto modified = concrete_type->AsModifiedType();
            modified && modified->tag() == DwarfTag::kPointerType) {
          if (auto modified_type = modified->modified().Get()->AsType())
            size = modified_type->byte_size();
        }

        // Convert anything else <= 64 bits to a number.
        uint64_t address = 0;
        Err conversion_err = value.value().PromoteTo64(&address);
        cb(conversion_err, address, size);
      });
}

std::string FormatConsoleString(const std::string& input) {
  // The console parser accepts two forms:
  //  - A C-style string (raw or not) with quotes and C-style escape sequences.
  //  - A whitespace-separated string with no escape character handling.

  // Determine which of the cases is required.
  bool has_space = false;
  bool has_special = false;
  bool has_quote = false;
  for (unsigned char c : input) {
    if (isspace(c)) {
      has_space = true;
    } else if (c < ' ') {
      has_special = true;
    } else if (c == '"') {
      has_quote = true;
    }
    // We assume any high-bit characters are part of UTF-8 sequences. This isn't necessarily the
    // case. We could validate UTF-8 sequences but currently that effort isn't worth it.
  }

  if (!has_space && !has_special && !has_quote)
    return input;

  std::string result;
  if (has_quote && !has_special) {
    // Raw-encode strings with embedded quotes as long as nothing else needs escaping.

    // Make sure there's a unique delimiter in case the string has an embedded )".
    std::string delim;
    while (input.find(")" + delim + "\"") != std::string::npos)
      delim += "*";

    result = "R\"" + delim + "(";
    result += input;
    result += ")" + delim + "\"";
  } else {
    // Normal C string.
    result = "\"";
    for (char c : input)
      AppendCEscapedChar(c, &result);
    result += "\"";
  }
  return result;
}

}  // namespace zxdb
