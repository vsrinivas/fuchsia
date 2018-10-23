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

// quit-agent ------------------------------------------------------------------

const char kQuitAgentShortHelp[] = R"(quit-agent: Quits the debug agent.)";
const char kQuitAgentHelp[] =
    R"(quit-agent

  Quits the connected debug agent running on the target.)";

Err DoQuitAgent(ConsoleContext* context, const Command& cmd) {
  context->session()->QuitAgent([](const Err& err) {
    if (err.has_error()) {
      Console::get()->Output(err);
    } else {
      Console::get()->Output("Successfully stopped the debug agent.");
    }
  });

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

    // We call the given callback
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
  Initially, all objects default to their schemas, but values can be overridden
  for individual objects.

Instance Overrides

  Values overriding means that you can modify behaviour for a particular object.
  If a setting has not been overridden for that object, it will fallback to the
  settings of parent object. The fallback order is as follows:

  Thread -> Process -> System -> Schema Default

  This means that if a thread has not overridden a value, it will check if the
  owning process has overridden it, then is the system has overridden it. If
  there are none, it will get the default value of the thread schema.

  For example, if t1 has overridden "foo" but t2 has not:

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
      which ones are overridden.

  get -s
      List the values of settings at the system level.
  )";

constexpr int kGetSystemSwitch = 0;

Err DoGet(ConsoleContext* context, const Command& cmd) {
  std::string setting_name;
  if (!cmd.args().empty()) {
    if (cmd.args().size() > 1)
      return Err("Expected only one setting name");
    setting_name = cmd.args()[0];
  }

  Err err;
  OutputBuffer out;
  if (setting_name.empty()) {
    FormatSettings(cmd, &out);
  } else {
    err = FormatSetting(cmd, setting_name, &out);
  }

  if (err.has_error())
    return err;

  Console::get()->Output(std::move(out));
  return Err();
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
  When setting a value, if it is not qualified, it will be set the setting at
  the highest level it can, in order to make it as general as possible.

  In most cases these higher level will be system-wide, to change behavior to
  the whole system, that can be overriden per-process or per-thread. Sometimes
  though, the setting only makes sense on a per-object basis (eg. new process
  filters for jobs). In this case, the unqualified set will work on the current
  object in the context.

  In order to override a setting at a job, target or thread level, the setting
  command has to be explicitly qualified. This works for both avoiding setting
  the value at a global context or to set the value for an object other than
  the current one. See examples below.

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
  Set boolean_setting system-wide:
  true

  [zxdb] pr set int_setting 1024
  Set int_setting for process 2:
  1024

  [zxdb] p 3 t 2 set string_setting somesuperlongstring
  Set setting for thread 2 of process 3:
  somesuperlongstring

  [zxdb] get foo
  ...
  • first
  • second
  • third
  ...
  Set value: first:second:third
  [zxdb] set foo first:second:third:fourth
  Set foo for job 3:
  • first
  • second
  • third
  • fourth

  NOTE: In the last case, even though the setting was not qualified, it was
        set at the job level. This is because this is a job-specific setting
        that doesn't make sense system-wide, but rather only per job.
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

Err SetSetting(const std::string& setting_name, const std::string& value,
               SettingStore* store, StoredSetting* out) {
  if (!store->HasSetting(setting_name))
    return Err("Could not find setting %s", setting_name.data());

  Err err;
  switch (store->GetSetting(setting_name).value.type()) {
    case SettingType::kBoolean:
      err = SetBool(store, setting_name, value);
      break;
    case SettingType::kInteger:
      err = SetInt(store, setting_name, value);
      break;
    case SettingType::kString:
      err = store->SetString(setting_name, value);
      break;
    case SettingType::kList:
      err = store->SetList(
          setting_name, fxl::SplitStringCopy(value, ":", fxl::kKeepWhitespace,
                                             fxl::kSplitWantNonEmpty));
      break;
    case SettingType::kNull:
      return Err("Unknown type for setting %s. Please file a bug with repro.",
                 setting_name.data());
  }

  if (!err.ok())
    return err;

  *out = store->GetSetting(setting_name);
  return Err();
}

OutputBuffer FormatSetFeedback(const std::string& setting_name,
                               SettingSchema::Level level, int target_id,
                               int element_id = 0) {
  std::string message;
  switch (level) {
    case SettingSchema::Level::kSystem:
      message = fxl::StringPrintf("Set %s system wide:\n", setting_name.data());
      break;
    case SettingSchema::Level::kJob:
      message = fxl::StringPrintf("Set %s for job %d:\n", setting_name.data(),
                                  element_id);
      break;
    case SettingSchema::Level::kTarget:
      message = fxl::StringPrintf("Set %s for process %d:\n",
                                  setting_name.data(), element_id);
      break;
    case SettingSchema::Level::kThread:
      message = fxl::StringPrintf("Set %s for thread %d of process %d:\n",
                                  setting_name.data(), target_id, element_id);
      break;
    default:
      FXL_NOTREACHED() << "Should not receive a default setting.";
  }

  return OutputBuffer(std::move(message));
}

Err DoSet(ConsoleContext* context, const Command& cmd) {
  if (cmd.args().size() < 2) {
    return Err("Wrong amount of Arguments. See \"help set\".");
  }
const std::string& setting_name = cmd.args()[0];
  // TODO(donosoc): Support multi word strings.
  const std::string& value = cmd.args()[1];

  // We lookup which SettingStore should respond to this query.
  Console* console = Console::get();
  SettingStore* store = nullptr;
  Thread* thread = cmd.thread();
  Target* target = cmd.target();
  JobContext* job_context = cmd.job_context();

  // These indices are for giving better feedback to the user.
  int target_id = console->context().IdForTarget(target);
  int element_id = -1;  // ID of the element whose setting was set.

  if (!target)
    return Err("No target found. Please file a bug with a repro.");

  // If the user qualified the query, it means that we want to query *that*
  // specific SettingStore, so we search for it. We search from more specific
  // to less specific.
  if (cmd.HasNoun(Noun::kThread)) {
    store = thread ? &thread->settings() : nullptr;
    element_id = cmd.GetNounIndex(Noun::kThread);
  } else if (cmd.HasNoun(Noun::kProcess)) {
    store = &target->settings();
    element_id = cmd.GetNounIndex(Noun::kProcess);
  } else if (cmd.HasNoun(Noun::kJob)) {
    store = job_context ? &job_context->settings() : nullptr;
    element_id = cmd.GetNounIndex(Noun::kJob);
  }


  if (!store) {
    // We didn't found an explicit specified store, so we lookup in the current
    // context. We look from less specific to more specific in terms of stores,
    // so that the setting will be set for a biggest setting as it can.
    // NOTE(donosoc): Some settings are replicated upwards, even to the system
    //                level, as they make sense (eg. pause on attach). But
    //                others only make sense locally (eg. job filters).
    // TODO(donosoc): This works as long as the settings are not ambiguous. We
    //                should add a way to desambiguate the setting.
    if (!target)
      return Err("No target found. Please file a bug with a repro.");

    System& system = target->session()->system();

    if (system.settings().schema()->HasSetting(setting_name)) {
      store = &system.settings();
    } else if (job_context &&
               job_context->settings().schema()->HasSetting(setting_name)) {
      store = &job_context->settings();
      element_id = console->context().IdForJobContext(job_context);
    } else if (target->settings().schema()->HasSetting(setting_name)) {
      store = &target->settings();
      element_id = console->context().IdForTarget(target);
    } else if (thread &&
               thread->settings().schema()->HasSetting(setting_name)) {
      store = &thread->settings();
      element_id = console->context().IdForThread(thread);
    }
  }

  if (!store)
    return Err("Could not find setting \"%s\".", setting_name.data());

  StoredSetting out;  // Used for showing the new value.
  Err err = SetSetting(setting_name, value, store, &out);
  if (!err.ok())
    return err;

  // Should never override default (schema) values.
  FXL_DCHECK(out.level != SettingSchema::Level::kDefault);

  // We output the new value.

  Console::get()->Output(
      FormatSetFeedback(setting_name, out.level, target_id, element_id));
  Console::get()->Output(FormatSettingValue(out));
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
  (*verbs)[Verb::kDisconnect] =
      VerbRecord(&DoDisconnect, {"disconnect"}, kDisconnectShortHelp,
                 kDisconnectHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kQuitAgent] =
      VerbRecord(&DoQuitAgent, {"quit-agent"}, kQuitAgentShortHelp,
                 kQuitAgentHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kOpenDump] =
      VerbRecord(&DoOpenDump, {"opendump"}, kOpenDumpShortHelp, kOpenDumpHelp,
                 CommandGroup::kGeneral);
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
