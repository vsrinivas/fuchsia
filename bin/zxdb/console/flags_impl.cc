// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/flags.h"
#include "garnet/bin/zxdb/console/flags_impl.h"
#include "garnet/bin/zxdb/client/err.h"
#include "garnet/public/lib/fxl/files/file.h"
#include "garnet/public/lib/fxl/files/path.h"
#include "garnet/public/lib/fxl/strings/split_string.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

using Option = fxl::CommandLine::Option;

// A Flag consists in a standard set of description values.
// The quickest way to install a flag is to define those expected values as
// constants with a standard name and use the following macro in the flag
// initialization function.
//
// Basically it boils down to a set of descriptive strings that is used for
// flushing out the user messages and most importantly the Process function.
// The Process function has to have the signature of ProcessFlag, as that
// function will work as a dispatcher.
//
// The role of a flag Processing function is as follows:
// Generate all the actions needed to comply with the flag.
// Each generated action must be correctly prioritized. High priority should
// be used in cases in which an action would affect other actions. Use low
// priority otherwise.
//
// Each action can be passed a test callback in order to modify simulate
// some kind of flow.
//
// NOTE: All values must be defined for the macro to work, even if the flag
// doesn't use them all.
#define INSTALL_FLAG(flag_name) \
  flags.push_back({k##flag_name##Name, k##flag_name##LongForm,          \
                   k##flag_name##ShortForm, k##flag_name##LongHelp,     \
                   k##flag_name##ShortHelp, k##flag_name##ArgumentName, \
                   k##flag_name##DefaultValue});

// version ---------------------------------------------------------------------

const char* kVersionName = "Help";
const char* kVersionLongForm = "help";
const char* kVersionShortForm = "h";
const char* kVersionLongHelp = R"(Gets zxdb version)";
const char* kVersionShortHelp = R"(Gets zxdb version)";
const char* kVersionArgumentName = nullptr;
const char* kVersionDefaultValue = nullptr;

void PrintVersion() {
  // TODO(donosoc): Find a good way to do versioning
  printf("zxdb v0.1\n");
}

// help ------------------------------------------------------------------------

const char* kHelpName = "Help";
const char* kHelpLongForm = "help";
const char* kHelpShortForm = "h";
const char* kHelpLongHelp =
    R"(Display information about the flags available in the system.)";
const char* kHelpShortHelp = R"(Displays this help message.)";
const char* kHelpArgumentName = "OPTION";
const char* kHelpDefaultValue = "";

Err PrintHelp(const std::string& cmd_name) {
  if (!cmd_name.empty()) {
    const FlagRecord* cmd_flag = GetFlagFromName(cmd_name);
    if (cmd_flag) {
      std::string desc = GetFlagLongDescription(*cmd_flag);
      printf("%s\n", desc.c_str());
    } else {
      return Err(fxl::StringPrintf("Unrecognized flag \"%s\"",
                                   cmd_name.c_str()));
    }
  } else {
    // We're asking for generic help, so we print all the flags' short help.
    std::stringstream ss;
    ss << "Usage: zxdb [OPTION ...]\n\n";
    ss << "options:" << std::endl;
    for (const auto& f : GetFlags()) {
      ss << GetFlagSignature(f) << ": " << f.short_help << std::endl;
    }
    printf("%s\n", ss.str().c_str());
  }
  return Err();
}

// script-file -----------------------------------------------------------------

const char* kScriptFileName = "Script File"; const char* kScriptFileLongForm = "script-file";
const char* kScriptFileShortForm = "S";
const char* kScriptFileLongHelp =
    R"(Reads a script file from a file.
    The file must contains valid zxdb commands as they would be input from the command line.
    They will be executed sequentially.)";
const char* kScriptFileShortHelp = R"(Loads and run script file.)";
const char* kScriptFileArgumentName = "SCRIPT-FILE";
const char* kScriptFileDefaultValue = nullptr;

// Define mock variable
std::string kScriptFileMockContents = "";

// The callback and mock contents are for ease of testing
Err ProcessScriptFile(const std::string& path, std::vector<Action>* actions,
                      const std::string& mock_contents) {
  // We read and split per line the contents of the given filepath
  // We see if there are mock
  std::string contents = !mock_contents.empty() ? mock_contents : std::string();
  if (contents.empty()) {
    if (!files::ReadFileToString(files::AbsolutePath(path), &contents)) {
      return Err(fxl::StringPrintf("Could not read file \"%s\"", path.c_str()));
    }
  }

  auto commands = fxl::SplitStringCopy(contents, "\n", fxl::kTrimWhitespace,
                                       fxl::kSplitWantNonEmpty);
  // We append them with decreasing priority
  for (size_t i = 0; i < commands.size(); i++) {
    actions->push_back(Action(commands[i], [&, cmd = commands[i]](
                                               const Action& action,
                                               const Session& session,
                                               Console* console) {
      console->ProcessInputLine(cmd.c_str(), ActionFlow::PostActionCallback);
    }));
  }
  return Err();
}

// Flag Registration / Processing  ---------------------------------------------

std::vector<FlagRecord> InitializeFlags() {
  std::vector<FlagRecord> flags;
  INSTALL_FLAG(Help);
  INSTALL_FLAG(ScriptFile);

  // We sort the flags
  std::sort(flags.begin(), flags.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.name < rhs.name;
  });

  return flags;
}

}   // namespace zxdb
