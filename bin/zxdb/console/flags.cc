// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <sstream>

#include "garnet/bin/zxdb/console/flags.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

using Option = fxl::CommandLine::Option;

// A Flag consists in a standard set of description values.
// The quickest way to install a flag is to define those expected values as
// constants with a standard name and use the following macro in the flag
// initialization function.
//
// NOTE: All values must be defined for the macro to work, even if the flag
// doesn't use them.
#define INSTALL_FLAG(flag_name)                                         \
  flags.push_back({k##flag_name##Name, k##flag_name##LongForm,          \
                   k##flag_name##ShortForm, k##flag_name##LongHelp,     \
                   k##flag_name##ShortHelp, k##flag_name##ArgumentName, \
                   k##flag_name##DefaultValue})
// help ------------------------------------------------------------------------

const char* kHelpName = "Help";
const char* kHelpLongForm = "help";
const char* kHelpShortForm = "h";
const char* kHelpLongHelp =
    R"(Display information about the flags available in the system.)";
const char* kHelpShortHelp = R"(Displays this help message.)";
const char* kHelpArgumentName = "OPTION";
const char* kHelpDefaultValue = "";

// -----------------------------------------------------------------------------

std::vector<FlagRecord> InitializeFlags(
    const std::vector<FlagRecord>* mock_flags) {
  if (mock_flags) {
    return *mock_flags;
  }

  std::vector<FlagRecord> flags;
  INSTALL_FLAG(Help);

  // We sort the flags
  std::sort(flags.begin(), flags.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.name < rhs.name;
  });

  return flags;
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

#define SEPARATOR "\n\n"

std::string GetFlagSignature(const FlagRecord& flag) {
  std::stringstream ss;
  ss << "--" << flag.long_form;

  // If the flag has default_value set, it means that the value is optional,
  // so we display it with [] instead of <>.
  char var_bracket_open = flag.default_value ? '[' : '<';
  char var_bracked_close = flag.default_value ? ']' : '>';

  // Arguments
  if (flag.argument_name) {
    ss << " " << var_bracket_open << flag.argument_name << var_bracked_close;
  }
  return ss.str();
}

std::string GetFlagLongDescription(const FlagRecord& flag) {
  std::stringstream ss;

  ss << flag.name << std::endl;
  ss << "Usage: " << GetFlagSignature(flag) << SEPARATOR;
  ss << flag.long_help << std::endl;

  return ss.str();
}

Err GenerateHelp(const Option& option, std::string* out, bool* quit) {
  // Help always quits
  *quit = true;

  // We see if we're asking for specific information on a specific flag.
  if (!option.value.empty()) {
    const FlagRecord* flag = GetFlagFromName(option.value);
    if (flag) {
      *out = GetFlagLongDescription(*flag);
      return Err();
    } else {
      return Err(
          fxl::StringPrintf("Unrecognized flag \"%s\"", option.value.c_str()));
    }
  }

  // We're asking for generic help, so we print all the flags' short help.
  std::stringstream ss;
  ss << "Usage: zxdb [OPTION ...]" << SEPARATOR;
  ss << "options:" << std::endl;
  for (const auto& flag : GetFlags()) {
    ss << GetFlagSignature(flag) << ": " << flag.short_help << std::endl;
  }

  *out = ss.str();
  return Err();
}

Err ProcessFlag(const Option& option, const FlagRecord& flag,
                std::string* out) {
  if (!flag.argument_name) {
    // We check if we got an argument we weren't expecting
    if (!option.value.empty()) {
      return Err(fxl::StringPrintf("Flag \"%s\" doesn't receive arguments.",
                                   flag.long_form));
    }
  } else {
    if (!flag.default_value) {
      if (option.value.empty()) {
        return Err(fxl::StringPrintf("Flag \"%s\" expects an argument.",
                                     flag.long_form));
      }
    }
  }

  return Err();
}

}  // namespace

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

const std::vector<FlagRecord>& SetupFlagsOnce(
    const std::vector<FlagRecord>* mock_flags) {
  static auto flags = InitializeFlags(mock_flags);
  return flags;
}

const std::vector<FlagRecord>& GetFlags() { return SetupFlagsOnce(); }

Err ProcessCommandLine(const fxl::CommandLine& cmd_line, std::string* out,
                       bool* quit) {
  // Normally we don't quit.
  *quit = false;

  // We look for help first.
  size_t help_index;
  if (cmd_line.HasOption("help", &help_index)) {
    // Out will be filled with the output or and error message
    return GenerateHelp(cmd_line.options()[help_index], out, quit);
  }

  // We see if we found an unexistent flag.
  for (const Option& option : cmd_line.options()) {
    const FlagRecord* flag = GetFlagFromOption(option);
    if (!flag) {
      return Err(
          fxl::StringPrintf("Unrecognized flag \"%s\"", option.name.c_str()));
    }

    return ProcessFlag(option, *flag, out);
  }

  // No erronous flags, no message returned.
  *out = std::string();
  return Err();
}

}  // namespace zxdb
