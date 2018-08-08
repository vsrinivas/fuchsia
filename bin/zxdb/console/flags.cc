// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <sstream>

#include "garnet/bin/zxdb/console/flags.h"
#include "garnet/bin/zxdb/console/flags_impl.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

using Option = fxl::CommandLine::Option;

namespace {

// This is to make sure we don't overwrite some results because we didn't check
// the previous result. Higher priority always wins.
FlagProcessResult UpdateResult(FlagProcessResult prev, FlagProcessResult cur) {
  return std::max(prev, cur);
}

}  // namespace

// Flag Processing -------------------------------------------------------------

namespace {

Err VerifyFlag(const FlagRecord& flag, const std::string& value) {
  if (!flag.argument_name) {
    // We check if we got an argument we weren't expecting
    if (!value.empty()) {
      return Err(fxl::StringPrintf("Flag \"%s\" doesn't receive arguments.",
                                   flag.long_form));
    }
  } else {
    if (!flag.default_value) {
      if (value.empty()) {
        return Err(fxl::StringPrintf("Flag \"%s\" expects an argument.",
                                     flag.long_form));
      }
    }
  }
  return Err();
}

Err VerifyFlags(const fxl::CommandLine& cmd_line) {
  // We see if we found an unexistent flag.
  for (const Option& option : cmd_line.options()) {
    const FlagRecord* flag = GetFlagFromOption(option);
    if (!flag) {
      return Err(
          fxl::StringPrintf("Unrecognized flag \"%s\"", option.name.c_str()));
    }

    Err flag_err = VerifyFlag(*flag, option.value);
    if (flag_err.has_error()) {
      return flag_err;
    }
  }

  // Remove this when positional arguments are used
  if (!cmd_line.positional_args().empty()) {
    const std::string& pos_arg = cmd_line.positional_args().front();
    return Err(fxl::StringPrintf("Unrecognized flag \"%s\"", pos_arg.c_str()));
  }

  return Err();
}

}  // namespace

FlagProcessResult ProcessCommandLine(const fxl::CommandLine& cmd_line,
                                     Err* out_err,
                                     std::vector<Action>* actions) {
  Err flag_err = VerifyFlags(cmd_line);

  // Check for errors.
  if (flag_err.has_error()) {
    *out_err = flag_err;
    return FlagProcessResult::kError;
  }

  // NOTE: Flags should be processed by priority.
  size_t flag_index;
  if (cmd_line.HasOption("version", &flag_index)) {
    PrintVersion();
    return FlagProcessResult::kQuit;
  }

  if (cmd_line.HasOption("help", &flag_index)) {
    const Option& option = cmd_line.options()[flag_index];
    Err err = PrintHelp(option.value);
    if (err.has_error()) {
      *out_err = err;
      return FlagProcessResult::kError;
    }
    return FlagProcessResult::kQuit;
  }

  // These flags append results, so they should not trump a previous result.
  FlagProcessResult res = FlagProcessResult::kContinue;
  if (cmd_line.HasOption("connect", &flag_index)) {
    const Option& option = cmd_line.options()[flag_index];
    Err err = ProcessConnect(option.value, actions);
    if (err.has_error()) {
      *out_err = err;
      return FlagProcessResult::kError;
    }
    res = UpdateResult(res, FlagProcessResult::kActions);
  }

  if (cmd_line.HasOption("run", &flag_index)) {
    const Option& option = cmd_line.options()[flag_index];
    Err err = ProcessRun(option.value, actions);
    if (err.has_error()) {
      *out_err = err;
      return FlagProcessResult::kError;
    }
    res = UpdateResult(res, FlagProcessResult::kActions);
  }

  if (cmd_line.HasOption("script-file", &flag_index)) {
    const Option& option = cmd_line.options()[flag_index];
    // We pass the global action callback for the action linking.
    Err err = ProcessScriptFile(option.value, actions);
    if (err.has_error()) {
      *out_err = err;
      return FlagProcessResult::kError;
    }
    res = UpdateResult(res, FlagProcessResult::kActions);
  }

  *out_err = Err();
  return res = UpdateResult(res, FlagProcessResult::kContinue);
}

// FlagRecord ------------------------------------------------------------------

FlagRecord::FlagRecord(const char* name, const char* long_form,
                       const char* short_form, const char* long_help,
                       const char* short_help, const char* argument_name,
                       const char* default_value)
    : name(name),
      long_form(long_form),
      short_form(short_form),
      long_help(long_help),
      short_help(short_help),
      argument_name(argument_name),
      default_value(default_value) {}

// Flag Helpers ----------------------------------------------------------------

const std::vector<FlagRecord>& GetFlags() {
  static auto flags = InitializeFlags();
  return flags;
}

void OverrideFlags(const std::vector<FlagRecord>& mock_flags) {
  const auto& flags = GetFlags();
  auto& nc = const_cast<std::vector<FlagRecord>&>(flags);
  nc = mock_flags;
}

const FlagRecord* GetFlagFromName(const std::string& name) {
  for (const auto& flag : GetFlags()) {
    if (flag.long_form == name) {
      return &flag;
    }
  }
  return nullptr;
}

const FlagRecord* GetFlagFromOption(const Option& option) {
  return GetFlagFromName(option.name);
}

std::string GetFlagLongDescription(const FlagRecord& flag) {
  std::stringstream ss;

  ss << flag.name << std::endl;
  ss << "Usage: " << GetFlagSignature(flag) << "\n\n";
  ss << flag.long_help << std::endl;

  return ss.str();
}

std::string GetFlagSignature(const FlagRecord& flag) {
  std::stringstream ss;
  ss << "--" << flag.long_form;

  // If the flag has default_value set, it means that the value is optional,
  // so we display it with [] instead of <>.
  char var_bracket_open = flag.default_value ? '[' : '<';
  char var_bracked_close = flag.default_value ? ']' : '>';

  // Arguments.
  if (flag.argument_name) {
    ss << " " << var_bracket_open << flag.argument_name << var_bracked_close;
  }
  return ss.str();
}

}  // namespace zxdb
