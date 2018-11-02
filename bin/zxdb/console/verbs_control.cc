// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <stdlib.h>

#include <algorithm>

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/setting_schema.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_settings.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/strings/trim.h"

namespace zxdb {

namespace {

// help ------------------------------------------------------------------------

const char kHelpShortHelp[] = R"(help / h: Help.)";
const char kHelpHelp[] =
    R"(help

  Yo dawg, I heard you like help on your help so I put help on the help in
  the help.)";

const char kHelpIntro[] =
    R"(Help!

  Type "help <topic>" for more information.

Command syntax

  Verbs
      "step"
          Applies the "step" verb to the currently selected thread.
      "mem-read --size=16 0x12345678"
          Pass a named switch and an argument.

  Nouns
      "thread"
          List available threads
      "thread 1"
          Select thread with ID 1 to be the default.

  Noun-Verb combinations
      "thread 4 step"
          Steps thread 4 of the current process regardless of the currently
          selected thread.
      "process 1 thread 4 step"
          Steps thread 4 of process 1 regardless of the currently selected
          thread or process.
)";

std::string FormatGroupHelp(const char* heading,
                            std::vector<std::string>* items) {
  std::sort(items->begin(), items->end());

  std::string help("\n");
  help.append(heading);
  help.append("\n");
  for (const auto& line : *items)
    help += "    " + line + "\n";
  return help;
}

std::string GetReference() {
  std::string help = kHelpIntro;

  // Group all verbs by their CommandGroup. Add nouns to this since people
  // will expect, for example, "breakpoint" to be in the breakpoints section.
  std::map<CommandGroup, std::vector<std::string>> groups;

  // Get the separate noun reference and add to the groups.
  help += "\nNouns\n";
  std::vector<std::string> noun_lines;
  for (const auto& pair : GetNouns()) {
    noun_lines.push_back(pair.second.short_help);
    groups[pair.second.command_group].push_back(pair.second.short_help);
  }
  std::sort(noun_lines.begin(), noun_lines.end());
  for (const auto& line : noun_lines)
    help += "    " + line + "\n";

  // Add in verbs.
  for (const auto& pair : GetVerbs())
    groups[pair.second.command_group].push_back(pair.second.short_help);

  help += FormatGroupHelp("General", &groups[CommandGroup::kGeneral]);
  help += FormatGroupHelp("Process", &groups[CommandGroup::kProcess]);
  help += FormatGroupHelp("Assembly", &groups[CommandGroup::kAssembly]);
  help += FormatGroupHelp("Breakpoint", &groups[CommandGroup::kBreakpoint]);
  help += FormatGroupHelp("Query", &groups[CommandGroup::kQuery]);
  help += FormatGroupHelp("Step", &groups[CommandGroup::kStep]);

  return help;
}

Err DoHelp(ConsoleContext* context, const Command& cmd) {
  OutputBuffer out;

  if (cmd.args().empty()) {
    // Generic help, list topics and quick reference.
    out.FormatHelp(GetReference());
    Console::get()->Output(std::move(out));
    return Err();
  }
  const std::string& on_what = cmd.args()[0];

  const char* help = nullptr;

  // Check for a noun.
  const auto& string_noun = GetStringNounMap();
  auto found_string_noun = string_noun.find(on_what);
  if (found_string_noun != string_noun.end()) {
    // Find the noun record to get the help. This is guaranteed to exist.
    const auto& nouns = GetNouns();
    help = nouns.find(found_string_noun->second)->second.help;
  } else {
    // Check for a verb
    const auto& string_verb = GetStringVerbMap();
    auto found_string_verb = string_verb.find(on_what);
    if (found_string_verb != string_verb.end()) {
      // Find the verb record to get the help. This is guaranteed to exist.
      const auto& verbs = GetVerbs();
      help = verbs.find(found_string_verb->second)->second.help;
    } else {
      // Not a valid command.
      out.OutputErr(Err("\"" + on_what +
                        "\" is not a valid command.\n"
                        "Try just \"help\" to get a list."));
      Console::get()->Output(std::move(out));
      return Err();
    }
  }

  out.FormatHelp(help);
  Console::get()->Output(std::move(out));
  return Err();
}

// quit ------------------------------------------------------------------------

const char kQuitShortHelp[] = R"(quit / q: Quits the debugger.)";
const char kQuitHelp[] =
    R"(quit

  Quits the debugger.)";

Err DoQuit(ConsoleContext* context, const Command& cmd) {
  // This command is special-cased by the main loop so it shouldn't get
  // executed.
  return Err();
}

// connect ---------------------------------------------------------------------

const char kConnectShortHelp[] =
    R"(connect: Connect to a remote system for debugging.)";
const char kConnectHelp[] =
    R"(connect <remote_address>

  Connects to a debug_agent at the given address/port. Both IP address and port
  are required.

  See also "disconnect".

Addresses

  Addresses can be of the form "<host> <port>" or "<host>:<port>". When using
  the latter form, IPv6 addresses must be [bracketed]. Otherwise the brackets
  are optional.

Examples

  connect mystem.localnetwork 1234
  connect mystem.localnetwork:1234
  connect 192.168.0.4:1234
  connect 192.168.0.4 1234
  connect [1234:5678::9abc] 1234
  connect 1234:5678::9abc 1234
  connect [1234:5678::9abc]:1234
)";

Err DoConnect(ConsoleContext* context, const Command& cmd,
              CommandCallback callback = nullptr) {
  // Can accept either one or two arg forms.
  std::string host;
  uint16_t port = 0;

  if (cmd.args().size() == 0) {
    return Err(ErrType::kInput, "Need host and port to connect to.");
  } else if (cmd.args().size() == 1) {
    Err err = ParseHostPort(cmd.args()[0], &host, &port);
    if (err.has_error())
      return err;
  } else if (cmd.args().size() == 2) {
    Err err = ParseHostPort(cmd.args()[0], cmd.args()[1], &host, &port);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput, "Too many arguments.");
  }

  context->session()->Connect(host, port, [callback, cmd](const Err& err) {
    if (err.has_error()) {
      // Don't display error message if they canceled the connection.
      if (err.type() != ErrType::kCanceled)
        Console::get()->Output(err);
    } else {
      OutputBuffer msg;
      msg.Append("Connected successfully.\n");
      cmd.job_context()->AttachToComponentRoot(nullptr);

      // Assume if there's a callback this is not being run interactively.
      // Otherwise, show the usage tip.
      if (!callback) {
        msg.Append(Syntax::kWarning, "👉 ");
        msg.Append(Syntax::kComment,
                   "Normally you will \"run <program path>\" or \"attach "
                   "<process koid>\".");
      }
      Console::get()->Output(std::move(msg));
    }

    if (callback)
      callback(err);
  });
  Console::get()->Output("Connecting (use \"disconnect\" to cancel)...\n");

  return Err();
}

// opendump --------------------------------------------------------------------

const char kOpenDumpShortHelp[] =
    R"(opendump: Open a dump file for debugging.)";
const char kOpenDumpHelp[] =
    R"(opendump <path>

  Opens a minidump file. Currently only the 'minidump' format is supported.
)";

Err DoOpenDump(ConsoleContext* context, const Command& cmd,
               CommandCallback callback = nullptr) {
  std::string path;

  if (cmd.args().size() == 0) {
    return Err(ErrType::kInput, "Need path to open.");
  } else if (cmd.args().size() == 1) {
    path = cmd.args()[0];
  } else {
    return Err(ErrType::kInput, "Too many arguments.");
  }

  context->session()->OpenMinidump(path, [callback](const Err& err) {
    if (err.has_error()) {
      Console::get()->Output(err);
    } else {
      Console::get()->Output("Dump loaded successfully.\n");
    }

    if (callback)
      callback(err);
  });
  Console::get()->Output("Opening dump file...\n");

  return Err();
}

// disconnect ------------------------------------------------------------------

const char kDisconnectShortHelp[] =
    R"(disconnect: Disconnect from the remote system.)";
const char kDisconnectHelp[] =
    R"(disconnect

  Disconnects from the remote system, or cancels an in-progress connection if
  there is one.

  There are no arguments.
)";

Err DoDisconnect(ConsoleContext* context, const Command& cmd,
                 CommandCallback callback = nullptr) {
  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"disconnect\" takes no arguments.");

  context->session()->Disconnect([callback](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
    else
      Console::get()->Output("Disconnected successfully.");

    // We call the given callbasck
    if (callback)
      callback(err);
  });

  return Err();
}

// cls -------------------------------------------------------------------------

const char kClsShortHelp[] = "cls: clear screen.";
const char kClsHelp[] =
    R"(cls

  Clears the contents of the console. Similar to "clear" on a shell.

  There are no arguments.
)";

Err DoCls(ConsoleContext* context, const Command& cmd,
          CommandCallback callback = nullptr) {
  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"cls\" takes no arguments.");

  Console::get()->Clear();

  if (callback)
    callback(Err());
  return Err();
}

// get -------------------------------------------------------------------------

const char kGetShortHelp[] = "get: Get a setting(s) value(s).";
const char kGetHelp[] =
    R"(get (--system|-s) [setting_name]

  Gets the value of all the settings or the detailed description of one.

Arguments

  --system|-s
      Refer to the system context instead of the current one.
      See below for more details.

  [setting_name]
      Filter for one setting. Will show detailed information, such as a
      description and more easily copyable values.

Setting Types

  Settings have a particular type: bool, int, string or list (of strings).
  The type is set beforehand and cannot change. Getting the detailed information
  of a setting will show the type of setting it is, though normally it is easy
  to tell from the list of values.

Contexts

  Within zxdb, there is the concept of the current context. This means that at
  any given moment, there is a current process, thread and breakpoint. This also
  applies when handling settings. By default, get will query the settings for
  the current thread. If you want to query the settings for the current target
  or system, you need to qualify at such.

  There are currently 3 contexts where settings live:

  - System
  - Target (roughly equivalent to a Process, but remains even when not running).
  - Thread

  In order to query a particular context, you need to qualify it:

  get foo
      Unqualified. Queries the current thread settings.
  p 1 get foo
      Qualified. Queries the selected process settings.
  p 3 t 2 get foo
      Qualified. Queries the selectedthread settings.

  For system settings, we need to override the context, so we need to explicitly
  ask for it. Any explicit context will be ignored in this case:

  get -s foo
      Retrieves the value of "foo" for the system.

Schemas

  Each setting level (thread, target, etc.) has an associated schema.
  This defines what settings are available for it and the default values.
  Initially, all objects default to their schemas, but values can be overriden
  for individual objects.

Instance Overrides

  Values overriding means that you can modify behaviour for a particular object.
  If a setting has not been overriden for that object, it will fallback to the
  settings of parent object. The fallback order is as follows:

  Thread -> Process -> System -> Schema Default

  This means that if a thread has not overriden a value, it will check if the
  owning process has overriden it, then is the system has overriden it. If
  there are none, it will get the default value of the thread schema.

  For example, if t1 has overriden "foo" but t2 has not:

  t 1 foo
      Gets the value of "foo" for t1.
  t 2 foo
      Queries the owning process for foo. If that process doesn't have it (no
      override), it will query the system. If there is no override, it will
      fallback to the schema default.

  NOTE:
  Not all settings are present in all schemas, as some settings only make sense
  in a particular context. If the thread schema holds a setting "foo" which the
  process schema does not define, asking for "foo" on a thread will only default
  to the schema default, as the concept of "foo" does not makes sense to a
  process.

Examples

  get
      List the global settings for the System context.

  p get foo
      Get the value of foo for the global Process context.

  p 2 t1 get
      List the values of settings for t1 of p2.
      This will list all the settings within the Thread schema, highlighting
      which ones are overriden.

  get -s
      List the values of settings at the system level.
  )";

Err HandleSettingStore(const SettingStore& store,
                       const std::string& setting_name) {
  OutputBuffer out;
  Err err = FormatSettings(store, setting_name, &out);
  if (err.has_error())
    return err;

  // If we find the values, we output them.
  Console::get()->Output(std::move(out));
  return Err();
}

constexpr int kGetSystemSwitch = 0;

Err DoGet(ConsoleContext* context, const Command& cmd) {
  std::string setting_name;
  if (!cmd.args().empty()) {
    if (cmd.args().size() > 1)
      return Err("Expected only one setting name");
    setting_name = cmd.args()[0];
  }

  Target* target = cmd.target();
  if (!target)
    return Err("No target found. Please file a bug with a repro.");

  // See if the user is asking for system-level settings.
  if (cmd.HasSwitch(kGetSystemSwitch)) {
    return HandleSettingStore(target->session()->system().settings(),
                              setting_name);
  }

  // First we check is the user is asking for process.
  if (cmd.HasNoun(Noun::kProcess) && !cmd.HasNoun(Noun::kThread))
    return HandleSettingStore(target->settings(), setting_name);

  Process* process = target->GetProcess();
  if (!process) {
    return Err(
        "Process not running, no threads. Is this a system setting? See \"help "
        "get\".");
  }

  Thread* thread = cmd.thread();
  if (!thread)
    return Err("Could not find specified thread.");
  return HandleSettingStore(thread->settings(), setting_name);
}

// Set -------------------------------------------------------------------------

constexpr int kSetSystemSwitch = 0;

const char kSetShortHelp[] = "set: Set a setting value.";
const char kSetHelp[] =
    R"(set <setting_name> <value>

  Sets the value of a setting.

Arguments

  <setting_name>
      The setting that will modified. Must match exactly.

  <value>
      The value to set. Keep in mind that settings have different types, so the
      value will be validated. Read more below.

Contexts, Schemas and Instance Overrides

  Settings have a hierarchical system of contexts where settings are defined.
  When setting a value, if it is not qualified, it will be set at a system
  level. In order to override it at a target or thread level, the setting
  command has to be explicitly qualified. See examples below.

  There is detailed information on contexts and schemas in "help get".

Setting Types

  Settings have a particular type: bool, int, string or list (of strings).
  The type is set beforehand and cannot change. Getting the detailed information
  of a setting will show the type of setting it is, though normally it is easy
  to tell from the list of valued.

  The valid inputs for each type are:

  - bool: "0", "false" -> false
          "1", "true"  -> true
  - int: Any string convertible to integer (think std::atoi).
  - string: Any one-word string. Working on getting multi-word strings.
  - list: List uses a representation of colon (:) separated values. While
          showing the list value uses bullet points, setting it requires the
          colon-separated representation. Running "get <setting_name>" will give
          the current "list setting value" for a list setting, which can be
          copy-pasted for easier editing. See example for a demostration.

Examples

  [zxdb] set boolean_setting true
  Set system-level setting:
  true

  [zxdb] pr set int_setting 1024
  Overrode setting for the given process:
  1024

  [zxdb] p 3 t 2 set string_setting somesuperlongstring
  Overrode setting for the given thread:
  somesuperlongstring

  [zxdb] get foo
  ...
  • first
  • second
  • third
  ...
  Set value: first:second:third
  [zxdb] set foo first:second:third:fourth
  • first
  • second
  • third
  • fourth
)";

Err SetBool(SettingStore* store, const std::string& setting_name,
            const std::string& value) {
  if (value == "0" || value == "false") {
    store->SetBool(setting_name, false);
  } else if (value == "1" || value == "true") {
    store->SetBool(setting_name, true);
  } else {
    return Err("%s expects a boolean. See \"help set\" for valid values.",
               setting_name.data());
  }
  return Err();
}

Err SetInt(SettingStore* store, const std::string& setting_name,
           const std::string& value) {
  int out;
  Err err = StringToInt(value, &out);
  if (err.has_error()) {
    return Err("%s expects a valid int: %s", setting_name.data(),
               err.msg().data());
  }

  return store->SetInt(setting_name, out);
}

Err SetSetting(SettingStore* store, const std::string& setting_name,
               const std::string& value) {
  StoredSetting setting = store->GetSetting(setting_name);
  if (setting.value.is_null())
    return Err("Could not find setting %s", setting_name.data());

  switch (setting.value.type()) {
    case SettingType::kBoolean:
      return SetBool(store, setting_name, value);
    case SettingType::kInteger:
      return SetInt(store, setting_name, value);
    case SettingType::kString:
      return store->SetString(setting_name, value);
    case SettingType::kList:
      return store->SetList(
          setting_name, fxl::SplitStringCopy(value, ":", fxl::kKeepWhitespace,
                                             fxl::kSplitWantNonEmpty));
    case SettingType::kNull:
      return Err("Unknown type for setting %s. Please file a bug with repro.",
                 setting_name.data());
  }
}

Err DoSet(ConsoleContext* context, const Command& cmd) {
  if (cmd.args().size() < 2) {
    return Err("Wrong amount of Arguments. See \"help set\".");
  }

  const std::string& setting_name = cmd.args()[0];
  // TODO(donosoc): Support multi word strings.
  const std::string& value = cmd.args()[1];

  Target* target = cmd.target();
  if (!target)
    return Err("No target found. Please file a bug with a repro.");

  // Lookup the correct setting store.
  SettingStore* store = nullptr;
  // We see if we have an explicit thread to set.
  if (cmd.HasNoun(Noun::kThread)) {
    Thread* thread = cmd.thread();
    if (!thread)
      return Err("Could not find specified thread.");
    store = &thread->settings();
  } else if (cmd.HasNoun(Noun::kProcess)) {
    Target* target = cmd.target();
    if (!target)
      return Err("Could not find specified target.");
    store = &target->settings();
  } else {
    // Finally, because no context was explicitly defined, we set the global
    // settings.
    Target* current_target = cmd.target();
    if (!current_target)
      return Err("Could not find current target. Please file bug with repro.");
    store = &target->session()->system().settings();
  }

  if (!store)
    return Err("Could not find a setting store. Please file a bug with repro.");

  Err err = SetSetting(store, setting_name, value);
  if (!err.ok())
    return err;

  // There should not be a default value.
  StoredSetting setting = store->GetSetting(setting_name, false);
  if (setting.value.is_null())
    return Err("Null setting after set. Please file a bug with repro.");

  // Should never override default (schema) values.
  FXL_DCHECK(setting.level != SettingStore::Level::kDefault);

  // Feedback about where the setting was set.
  if (setting.level == SettingStore::Level::kSystem) {
    Console::get()->Output("Set system-level setting:");
  } else {
    Console::get()->Output(
        fxl::StringPrintf("Overrode setting for the given %s:",
                          SettingStoreLevelToString(setting.level)));
  }

  // We output the new value.
  Console::get()->Output(FormatSettingValue(setting));
  return Err();
}

}  // namespace

void AppendControlVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kHelp] = VerbRecord(&DoHelp, {"help", "h"}, kHelpShortHelp,
                                     kHelpHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kQuit] = VerbRecord(&DoQuit, {"quit", "q"}, kQuitShortHelp,
                                     kQuitHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kConnect] =
      VerbRecord(&DoConnect, {"connect"}, kConnectShortHelp, kConnectHelp,
                 CommandGroup::kGeneral);
  (*verbs)[Verb::kOpenDump] =
      VerbRecord(&DoOpenDump, {"opendump"}, kOpenDumpShortHelp, kOpenDumpHelp,
                 CommandGroup::kGeneral);
  (*verbs)[Verb::kDisconnect] =
      VerbRecord(&DoDisconnect, {"disconnect"}, kDisconnectShortHelp,
                 kDisconnectHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kCls] = VerbRecord(&DoCls, {"cls"}, kClsShortHelp, kClsHelp,
                                    CommandGroup::kGeneral);

  // get.
  SwitchRecord get_system(kGetSystemSwitch, false, "system", 's');
  VerbRecord get(&DoGet, {"get"}, kGetShortHelp, kGetHelp,
                 CommandGroup::kGeneral);
  get.switches.push_back(std::move(get_system));
  (*verbs)[Verb::kGet] = std::move(get);

  // set.
  SwitchRecord set_system(kSetSystemSwitch, false, "system", 's');
  VerbRecord set(&DoSet, {"set"}, kSetShortHelp, kSetHelp,
                 CommandGroup::kGeneral);
  set.switches.push_back(std::move(set_system));
  (*verbs)[Verb::kSet] = std::move(set);
}

}  // namespace zxdb
