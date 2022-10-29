// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_utils.h"

#include <ctype.h>
#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>

#include <limits>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_location.h"
#include "src/developer/debug/zxdb/client/client_eval_context_impl.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_context.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/number_parser.h"
#include "src/developer/debug/zxdb/expr/return_value.h"
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

Err AssertStoppedThreadWithFrameCommand(ConsoleContext* context, const Command& cmd,
                                        const char* command_name, bool validate_nouns) {
  if (validate_nouns) {
    if (Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame}); err.has_error())
      return err;
  }

  if (!cmd.thread()) {
    return Err("\"%s\" requires a thread but there is no current thread.", command_name);
  }
  if (!cmd.thread()->CurrentStopSupportsFrames()) {
    return Err(
        "\"%s\" requires a suspended thread but thread %d is %s.\n"
        "To view and sync thread state with the remote system, type "
        "\"thread\".\nOr type \"pause\" to pause a running thread.",
        command_name, context->IdForThread(cmd.thread()),
        ThreadStateToString(cmd.thread()->GetState(), cmd.thread()->GetBlockedReason()).c_str());
  } else if (!cmd.frame()) {
    // Theoretically this shouldn't happen: if the thread is in a proper blocked state it should
    // have a frame. But we check this because callers will crash if the frame is not valid after
    // a successful return.
    FX_NOTREACHED() << "Thread has no frame but its state is "
                    << ThreadStateToString(cmd.thread()->GetState(),
                                           cmd.thread()->GetBlockedReason());
    return Err("Thread has no frame.");
  }

  return Err();
}

Err AssertAllStoppedThreadsCommand(ConsoleContext* context, const Command& cmd,
                                   const char* command_name, bool validate_nounds) {
  // For the threads to be stopped, the target needs to be running in the first place.
  if (Err err = AssertRunningTarget(context, command_name, cmd.target()); err.has_error())
    return err;
  Process* process = cmd.target()->GetProcess();

  std::vector<Thread*> threads = process->GetThreads();
  for (const Thread* thread : threads) {
    if (!thread->CurrentStopSupportsFrames()) {
      return Err(
          "\"%s\" requires all process threads be suspended but at least thread %d\n"
          "is not. Use \"pause\" to suspend the threads first.",
          command_name, context->IdForThread(thread));
    }
  }
  return Err();
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
  std::string trimmed(fxl::TrimString(s, " "));

  ErrOrValue number_value = StringToNumber(ExprLanguage::kC, trimmed);
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
  std::string trimmed(fxl::TrimString(s, " "));

  ErrOrValue number_value = StringToNumber(ExprLanguage::kC, trimmed);
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

std::string ThreadStateToString(std::optional<debug_ipc::ThreadRecord::State> state,
                                debug_ipc::ThreadRecord::BlockedReason blocked_reason) {
  if (!state)
    return "Unknown";

  // Blocked can have many cases, so we handle it separately.
  if (*state != debug_ipc::ThreadRecord::State::kBlocked)
    return debug_ipc::ThreadRecord::StateToString(*state);

  FX_DCHECK(blocked_reason != debug_ipc::ThreadRecord::BlockedReason::kNotBlocked)
      << "A blocked thread has to have a valid reason.";
  return fxl::StringPrintf("Blocked (%s)",
                           debug_ipc::ThreadRecord::BlockedReasonToString(blocked_reason));
}

std::string ExecutionScopeToString(const ConsoleContext* context, const ExecutionScope& scope) {
  switch (scope.type()) {
    case ExecutionScope::kSystem:
      return "global";
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
  FX_NOTREACHED();
  return std::string();
}

ExecutionScope ExecutionScopeForCommand(const Command& cmd) {
  if (cmd.HasNoun(Noun::kThread))
    return ExecutionScope(cmd.thread());  // Thread context given explicitly.
  if (cmd.HasNoun(Noun::kProcess))
    return ExecutionScope(cmd.target());  // Target context given explicitly.

  return ExecutionScope();  // Everything else becomes global scope.
}

Err ResolveBreakpointsForModification(const Command& cmd, const char* command_name,
                                      std::vector<Breakpoint*>* output) {
  output->clear();

  if (cmd.args().size() > 1) {
    return Err(ErrType::kInput,
               "Expecting zero or one arg for the location.\n"
               "Formats: <function>, <file>:<line#>, <line#>, or 0x<address>");
  }

  if (cmd.args().size() == 1) {
    // "bp <index> clear <location>" is pointless.
    Err err = cmd.ValidateNouns({});
    if (err.has_error())
      return err;

    // No need to resolve the location here because pending breakpoints only have input_locations.
    // As a result, if a user has breakpoints on both main and $main, "clear main" will only clear
    // the first breakpoint.
    std::vector<InputLocation> input_locations;
    if (Err err = ParseLocalInputLocation(cmd.frame(), cmd.args()[0], &input_locations);
        err.has_error())
      return err;

    ConsoleContext* context = &Console::get()->context();
    std::vector<Breakpoint*> breakpoints = context->session()->system().GetBreakpoints();

    for (Breakpoint* breakpoint : breakpoints) {
      // We compare the input_locations vector directly, in hopes that the same input will
      // resolve to the same order.
      if (input_locations == breakpoint->GetSettings().locations) {
        output->push_back(breakpoint);
      }
    }

    if (output->size() == 0) {
      auto msg = fxl::StringPrintf("\"%s\" matches zero breakpoints.", cmd.args()[0].c_str());
      if (cmd.args()[0].size() && isdigit(cmd.args()[0][0]))
        msg += fxl::StringPrintf(" Maybe you want to use \"bp %s %s\"?", cmd.args()[0].c_str(),
                                 command_name);
      return Err(msg);
    }
    return Err();
  }

  // When cmd.args().size() == 0, use the command's breakpoint context.
  Err err = cmd.ValidateNouns({Noun::kBreakpoint});
  if (err.has_error())
    return err;

  if (!cmd.breakpoint()) {
    return Err(
        fxl::StringPrintf("There is no active breakpoint and no breakpoint or location was given.\n"
                          "Use \"bp <index> %s\" or \"%s <location>\" to specify one.\n",
                          command_name, command_name));
  }

  output->push_back(cmd.breakpoint());
  return Err();
}

OutputBuffer FormatThread(const ConsoleContext* context, const Thread* thread) {
  OutputBuffer out("Thread ");
  out.Append(Syntax::kSpecial, std::to_string(context->IdForThread(thread)));

  out.Append(Syntax::kVariable, " state");
  out.Append("=" + FormatConsoleString(
                       ThreadStateToString(thread->GetState(), thread->GetBlockedReason())));

  out.Append(Syntax::kVariable, " koid");
  out.Append("=" + std::to_string(thread->GetKoid()));

  out.Append(Syntax::kVariable, " name");
  out.Append("=" + FormatConsoleString(thread->GetName()));

  return out;
}

OutputBuffer FormatBreakpoint(const ConsoleContext* context, const Breakpoint* breakpoint,
                              bool show_context) {
  BreakpointSettings settings = breakpoint->GetSettings();

  OutputBuffer result("Breakpoint ");
  result.Append(Syntax::kSpecial, std::to_string(context->IdForBreakpoint(breakpoint)) + " ");

  // Most breakpoints are simple global software breakpoints. To keep things easier to follow,
  // only show values that aren't the default.
  if (settings.scope.type() != ExecutionScope::kSystem) {
    result.Append(Syntax::kVariable, ClientSettings::Breakpoint::kScope);
    result.Append(std::string("=\"") + ExecutionScopeToString(context, settings.scope) + "\" ");
  }

  if (settings.stop_mode != BreakpointSettings::StopMode::kAll) {
    result.Append(Syntax::kVariable, ClientSettings::Breakpoint::kStopMode);
    result.Append(std::string("=") + BreakpointSettings::StopModeToString(settings.stop_mode) +
                  " ");
  }

  if (!settings.enabled) {
    result.Append(Syntax::kVariable, ClientSettings::Breakpoint::kEnabled);
    result.Append("=");
    if (settings.enabled) {
      result.Append("true");
    } else {
      // Highlight disabled breakpoints since that's an unusual condition.
      result.Append(Syntax::kError, "false");
    }
    result.Append(" ");
  }

  // Include type only for non-software (the normal ones) breakpoints.
  if (settings.type != BreakpointSettings::Type::kSoftware) {
    result.Append(Syntax::kVariable, ClientSettings::Breakpoint::kType);
    result.Append(std::string("=") + BreakpointSettings::TypeToString(settings.type) + " ");
  }

  if (BreakpointSettings::TypeHasSize(settings.type)) {
    result.Append(Syntax::kVariable, ClientSettings::Breakpoint::kSize);
    result.Append("=" + std::to_string(settings.byte_size) + " ");
  }

  if (settings.one_shot) {
    result.Append(Syntax::kVariable, ClientSettings::Breakpoint::kOneShot);
    result.Append(std::string("=") + BoolToString(settings.one_shot) + " ");
  }

  bool show_location_details = !settings.locations.empty() && show_context;

  size_t matched_locs = breakpoint->GetLocations().size();
  if (matched_locs == 0) {
    // When more details are being shown below, don't duplicate the "pending" warning.
    if (!show_location_details)
      result.Append(Syntax::kWarning, "pending ");
    result.Append("@ ");
  } else if (matched_locs == 1) {
    result.Append("@ ");
  } else {
    result.Append(fxl::StringPrintf("(%zu addrs) @ ", matched_locs));
  }
  result.Append(FormatInputLocations(settings.locations));
  result.Append("\n");

  if (show_location_details) {
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
      result.Append(
          ": No current matches for location. It will be matched against new\n"
          "         processes and shared libraries.\n");
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
      // Don't pass a TargetSymbols to FormatFileLine because we always want the full file name as
      // passed-in by the user (as this is an "input" location object). It is surprising if the
      // debugger deletes some input.
      return OutputBuffer(FormatFileLine(location.line));
    }
    case InputLocation::Type::kName: {
      FormatIdentifierOptions opts;
      opts.show_global_qual = true;  // Imporant to disambiguate for InputLocations.
      opts.bold_last = true;
      return FormatIdentifier(location.name, opts);
    }
    case InputLocation::Type::kAddress: {
      return OutputBuffer(to_hex_string(location.address));
    }
  }
  FX_NOTREACHED();
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
    FX_DCHECK(language_setting == ClientSettings::System::kLanguage_Auto);
  }

  // Target context only (it may or may not have a process).
  return fxl::MakeRefCounted<ClientEvalContextImpl>(cmd.target(), language);
}

Err EvalCommandExpression(const Command& cmd, const char* verb,
                          const fxl::RefPtr<EvalContext>& eval_context, bool follow_references,
                          bool verbose_errors, EvalCallback cb) {
  if (Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame}); err.has_error())
    return err;

  if (cmd.args().size() != 1)
    return Err("Usage: %s <expression>\nSee \"help %s\" for more.", verb, verb);

  EvalExpression(
      cmd.args()[0], std::move(eval_context), follow_references,
      [verbose_errors, cb = std::move(cb), verb = std::string(verb)](ErrOrValue result) mutable {
        if (verbose_errors && result.has_error()) {
          cb(RewriteCommandExpressionError(verb, result.err()));
        } else {
          cb(std::move(result));
        }
      });
  return Err();
}

Err EvalCommandAddressExpression(
    const Command& cmd, const char* verb, const fxl::RefPtr<EvalContext>& eval_context,
    fit::callback<void(const Err& err, uint64_t address, std::optional<uint32_t> size)> cb) {
  return EvalCommandExpression(
      cmd, verb, eval_context, true, true,
      [eval_context, verb = std::string(verb), cb = std::move(cb)](ErrOrValue value) mutable {
        if (value.has_error())
          return cb(value.err(), 0, std::nullopt);

        uint64_t address = 0;
        std::optional<uint32_t> size;

        if (Err err = ValueToAddressAndSize(eval_context, value.value(), &address, &size);
            err.has_error())
          return cb(RewriteCommandExpressionError(verb, err), 0, std::nullopt);
        cb(Err(), address, size);
      });
}

// Errors from the evaluation of expressions of commands oftem don't make sense without context.
Err RewriteCommandExpressionError(const std::string& verb, const Err& err) {
  if (err.type() == ErrType::kOptimizedOut) {
    // The common error messagess "unavailable" and "optimized out" (both sharing kOptimizedOut) are
    // very short because they're often included in long dumps of structures and local variables.
    // But it makes this common class of errors very mysterious here.
    return Err(
        "This variable is %s. Nothing has happened.\n"
        "See \"help expressions\" for more on what this means and what to do.",
        err.msg().c_str());
  }

  // All other errors.
  std::string effective_verb = verb.empty() ? std::string("the command") : verb;
  return Err("Unable to evaluate the expression for " + effective_verb + ". The result was:\n  " +
             err.msg());
}

std::string FormatConsoleString(const std::string& input) {
  // The console parser accepts two forms:
  //  - A C-style string (raw or not) with quotes and C-style escape sequences.
  //  - A whitespace-separated string with no escape character handling.

  if (input.empty())
    return std::string("\"\"");  // Empty strings need quotes.

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

ErrOr<Target*> GetRunnableTarget(ConsoleContext* context, const Command& cmd) {
  Target::State state = cmd.target()->GetState();
  if (state == Target::State::kNone)
    return cmd.target();  // Current one is usable.

  if (cmd.GetNounIndex(Noun::kProcess) != Command::kNoIndex) {
    // A process was specified explicitly in the command. Since it's not usable, report an error.
    if (state == Target::State::kStarting || state == Target::State::kAttaching) {
      return Err(
          "The specified process is in the process of starting or attaching.\n"
          "Either \"kill\" it or create a \"new\" process context.");
    }
    return Err(
        "The specified process is already running.\n"
        "Either \"kill\" it or create a \"new\" process context.");
  }

  // Create a new target based on the given one.
  Target* new_target = context->session()->system().CreateNewTarget(cmd.target());
  context->SetActiveTarget(new_target);
  return new_target;
}

Err VerifySystemHasRunningProcess(System* system) {
  for (const Target* target : system->GetTargets()) {
    if (target->GetProcess())
      return Err();
  }
  return Err("No processes are running.");
}

void ProcessCommandCallback(fxl::WeakPtr<Target> target, bool display_message_on_success,
                            const Err& err, fxl::RefPtr<CommandContext> cmd_context) {
  if (err.has_error()) {
    cmd_context->ReportError(err);
  } else if (ConsoleContext* console_context = cmd_context->GetConsoleContext();
             console_context && display_message_on_success) {
    cmd_context->Output(FormatTarget(console_context, target.get()));
  }
}

void AsyncPrintReturnValue(const FunctionReturnInfo& info, fit::deferred_callback cb) {
  // This only works for symbolized functions.
  const Function* func = info.symbol.Get()->As<Function>();
  if (!func)
    return;

  const Stack& stack = info.thread->GetStack();
  if (stack.empty())
    return;  // Something is messed up.
  auto eval_context = stack[0]->GetEvalContext();

  GetReturnValue(eval_context, func,
                 [eval_context, func = RefPtrTo(func), cb = std::move(cb)](ErrOrValue val) mutable {
                   if (val.has_error() || !val.value().type())
                     return;  // Error or void.

                   auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();

                   FormatFunctionNameOptions func_name_options;
                   func_name_options.name.elide_templates = true;
                   func_name_options.name.bold_last = true;
                   func_name_options.params = FormatFunctionNameOptions::kNoParams;
                   out->Append(FormatFunctionName(func.get(), func_name_options));
                   out->Append(Syntax::kOperatorBold, " 🡲 ");

                   ConsoleFormatOptions val_options;
                   val_options.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
                   val_options.wrapping = ConsoleFormatOptions::Wrapping::kSmart;
                   val_options.max_depth = 3;

                   out->Append(FormatValueForConsole(val.value(), val_options, eval_context));

                   out->Complete();
                   if (out->is_complete()) {
                     Console::get()->Output(out->DestructiveFlatten());
                   } else {
                     out->SetCompletionCallback([out = std::move(out), cb = std::move(cb)]() {
                       Console::get()->Output(out->DestructiveFlatten());
                     });
                   }
                 });
}

void ScheduleAsyncPrintReturnValue(const FunctionReturnInfo& info) {
  info.thread->AddPostStopTask(
      [weak_thread = info.thread->GetWeakPtr(), info](fit::deferred_callback cb) {
        // The FunctionReturnInfo has a thread pointer in it and we need to be sure it stays in
        // scope before continuing, even though we don't use the weak pointer directly.
        if (!weak_thread)
          return;
        AsyncPrintReturnValue(info, std::move(cb));
      });
}

}  // namespace zxdb
