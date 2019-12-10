// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/verbs_settings.h"

#include <ctype.h>

#include <algorithm>

#include "src/developer/debug/zxdb/client/execution_scope.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/adapters.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_parser.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_settings.h"
#include "src/developer/debug/zxdb/console/nouns.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Struct to represents all the context needed to correctly reason about the settings commands.
struct SettingContext {
  enum class Level {
    kGlobal,
    kJob,
    kTarget,
    kThread,
  };

  SettingStore* store = nullptr;

  // What kind of setting this is.
  Setting setting;

  // At what level the setting was applied.
  Level level = Level::kGlobal;

  // What kind of operation this is for set commands.
  ParsedSetCommand::Operation op = ParsedSetCommand::kAssign;
};

bool CheckGlobal(ConsoleContext* context, const Command& cmd, const std::string& setting_name,
                 SettingContext* out) {
  if (!context->session()->system().settings().HasSetting(setting_name))
    return false;
  out->store = &context->session()->system().settings();
  out->level = SettingContext::Level::kGlobal;
  return true;
}

bool CheckJob(ConsoleContext* context, const Command& cmd, const std::string& setting_name,
              SettingContext* out) {
  if (!cmd.job_context() || !cmd.job_context()->settings().schema()->HasSetting(setting_name))
    return false;
  out->store = &cmd.job_context()->settings();
  out->level = SettingContext::Level::kJob;
  return true;
}

bool CheckTarget(ConsoleContext* context, const Command& cmd, const std::string& setting_name,
                 SettingContext* out) {
  if (!cmd.target()->settings().schema()->HasSetting(setting_name))
    return false;
  out->store = &cmd.target()->settings();
  out->level = SettingContext::Level::kTarget;
  return true;
}

bool CheckThread(ConsoleContext* context, const Command& cmd, const std::string& setting_name,
                 SettingContext* out) {
  if (!cmd.thread() || !cmd.thread()->settings().schema()->HasSetting(setting_name))
    return false;
  out->store = &cmd.thread()->settings();
  out->level = SettingContext::Level::kThread;
  return true;
}

// Applies the hierarchical rules for getting/setting a setting and fills the give SettingContext.
// It takes into account the noun overrides.
Err GetSettingContext(ConsoleContext* context, const Command& cmd, const std::string& setting_name,
                      SettingContext* out) {
  if (!cmd.target())
    return Err("No process found. Please file a bug with a repro.");

  // Handle noun overrides for getting/setting on specific objects.
  if (cmd.HasNoun(Noun::kThread)) {
    out->store = cmd.thread() ? &cmd.thread()->settings() : nullptr;
    out->level = SettingContext::Level::kThread;
  } else if (cmd.HasNoun(Noun::kProcess)) {
    out->store = &cmd.target()->settings();
    out->level = SettingContext::Level::kTarget;
  } else if (cmd.HasNoun(Noun::kGlobal)) {
    out->store = &context->session()->system().settings();
    out->level = SettingContext::Level::kGlobal;
  } else if (cmd.HasNoun(Noun::kJob)) {
    out->store = cmd.job_context() ? &cmd.job_context()->settings() : nullptr;
    out->level = SettingContext::Level::kJob;
  }

  if (out->store) {
    // Found an explicitly requested setting store.
    if (cmd.verb() == Verb::kSet)  // Use the generic definition from the schama.
      out->setting = out->store->schema()->GetSetting(setting_name).setting;
    else if (cmd.verb() == Verb::kGet)  // Use the specific value from the store.
      out->setting = out->store->GetSetting(setting_name);
    return Err();
  }

  // Didn't found an explicit specified store, so lookup in the current context. Since the
  // settings can be duplicated on different levels, we need to search in the order that makes
  // sense for the command.
  //
  // This array encodes the order we search from the most global to most specific store.
  //
  // TODO(brettw) this logic should not be here. This search should be encoded in the fallback
  // stores in each SettingStore so that the logic is guaranteed to match.
  using CheckFunction =
      bool (*)(ConsoleContext*, const Command&, const std::string&, SettingContext*);
  const CheckFunction kGlobalToSpecific[] = {
      &CheckGlobal,
      &CheckJob,
      &CheckTarget,
      &CheckThread,
  };

  if (cmd.verb() == Verb::kSet) {
    // When setting, choose the most global context the setting can apply to.
    for (const auto cur : kGlobalToSpecific) {
      if (cur(context, cmd, setting_name, out)) {
        out->setting = out->store->schema()->GetSetting(setting_name).setting;
        return Err();
      }
    }
  } else if (cmd.verb() == Verb::kGet) {
    // When getting, choose the most specific context the setting can apply to.
    for (const auto cur : Reversed(kGlobalToSpecific)) {
      if (cur(context, cmd, setting_name, out)) {
        // Getting additionally requires that the setting be non-null. We want to find the first
        // one that might apply.
        out->setting = out->store->GetSetting(setting_name);
        if (!out->setting.value.is_null())
          return Err();
      }
    }
  }

  return Err("Could not find setting \"%s\".", setting_name.data());
}

// get ---------------------------------------------------------------------------------------------

const char kGetShortHelp[] = "get: Get a setting(s) value(s).";
const char kGetHelp[] =
    R"(get [setting_name]

  Gets the value of all the settings or the detailed description of one.

Arguments

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
  the current thread. If you want to query the settings for the current process
  or globally, you need to qualify at such.

  There are currently 3 contexts where settings live:

  - Global
  - Process
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

  Each setting level (thread, global, etc.) has an associated schema.
  This defines what settings are available for it and the default values.
  Initially, all objects default to their schemas, but values can be overridden
  for individual objects.

Instance Overrides

  Values overriding means that you can modify behaviour for a particular object.
  If a setting has not been overridden for that object, it will fallback to the
  settings of parent object. The fallback order is as follows:

  Thread -> Process -> Global -> Schema Default

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

Err SettingToOutput(ConsoleContext* console_context, const Command& cmd, const std::string& key,
                    OutputBuffer* out) {
  SettingContext setting_context;
  if (Err err = GetSettingContext(console_context, cmd, key, &setting_context); err.has_error())
    return err;

  if (!setting_context.setting.value.is_null()) {
    *out = FormatSetting(console_context, setting_context.setting);
    return Err();
  }

  return Err("Could not find setting %s", key.c_str());
}

Err CompleteSettingsToOutput(const Command& cmd, ConsoleContext* context, OutputBuffer* out) {
  // Output in the following order: System -> Job -> Target -> Thread
  out->Append(OutputBuffer(Syntax::kHeading, "Global\n"));
  out->Append(FormatSettingStore(context, context->session()->system().settings()));
  out->Append("\n");

  if (JobContext* job = cmd.job_context(); job && !job->settings().schema()->empty()) {
    auto title = fxl::StringPrintf("Job %d\n", context->IdForJobContext(job));
    out->Append(OutputBuffer(Syntax::kHeading, std::move(title)));
    out->Append(FormatSettingStore(context, job->settings()));
    out->Append("\n");
  }

  if (Target* target = cmd.target(); target && !target->settings().schema()->empty()) {
    auto title = fxl::StringPrintf("Process %d\n", context->IdForTarget(target));
    out->Append(OutputBuffer(Syntax::kHeading, std::move(title)));
    out->Append(FormatSettingStore(context, target->settings()));
    out->Append("\n");
  }

  if (Thread* thread = cmd.thread(); thread && !thread->settings().schema()->empty()) {
    auto title = fxl::StringPrintf("Thread %d\n", context->IdForThread(thread));
    out->Append(OutputBuffer(Syntax::kHeading, std::move(title)));
    out->Append(FormatSettingStore(context, thread->settings()));
    out->Append("\n");
  }

  return Err();
}

Err DoGet(ConsoleContext* context, const Command& cmd) {
  std::string setting_name;
  if (!cmd.args().empty()) {
    if (cmd.args().size() > 1)
      return Err("Expected only one setting name");
    setting_name = cmd.args()[0];
  }

  Err err;
  OutputBuffer out;
  if (!setting_name.empty()) {
    err = SettingToOutput(context, cmd, setting_name, &out);
  } else {
    err = CompleteSettingsToOutput(cmd, context, &out);
  }

  if (err.has_error())
    return err;
  Console::get()->Output(out);
  return Err();
}

// set ---------------------------------------------------------------------------------------------

const char kSetShortHelp[] = "set: Set a setting value.";
const char kSetHelp[] =
    R"(set <setting_name> [ <modification-type> ] <value>*

  Sets the value of a setting.

Arguments

  <setting_name>
      The setting that will modified. Must match exactly.

  <modification-type>
      Operator that indicates how to mutate a list. For non-lists only = (the
      default) is supported:

      =   Replace the current contents (the default).
      +=  Append the given value to the list.
      -=  Search for the given value and remove it.

  <value>
      The value(s) to set, add, or remove. Multiple values for list settings
      are whitespace separated. You can "quote" strings to include literal
      spaces.

Contexts, Schemas and Instance Overrides

  Settings have a hierarchical system of contexts where settings are defined.
  When setting a value, if it is not qualified, it will be set the setting at
  the highest level it can, in order to make it as general as possible.

  In most cases these higher level will be system-wide, to change behavior to
  the whole system, that can be overridden per-process or per-thread. Sometimes
  though, the setting only makes sense on a per-object basis (eg. new process
  filters for jobs). In this case, the unqualified set will work on the current
  object in the context.

  In order to override a setting at a job, process or thread level, the setting
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

  bool
      "0", "false" -> false
      "1", "true"  -> true

  integer
      Any string convertible to integer.

  string
      Strings can be "quoted" to include whitespace. Otherwise whitespace is
      used as a delimiter.

  list
      A list of one or more string separated by whitespace. Strings can be
      "quoted" to include literal whitespace.

Examples

  [zxdb] set show-stdout true
  New value show-stdout system-wide:
  true

  [zxdb] pr 3 set vector-format double
  New value vector-format for process 3:
  double

  [zxdb] get build-dirs
  • /home/me/build
  • /home/me/other/out

  [zxdb] set build-dirs += /tmp/build
  New value build-dirs system-wide:
  • /home/me/build
  • /home/me/other/out
  • /tmp/build

  [zxdb] set build-dirs = /other/build/location /other/build2
  New value build-dirs system-wide:
  • /other/build/location
  • /other/build2
)";

Err SetBool(SettingStore* store, const std::string& setting_name, const std::string& value) {
  if (value == "0" || value == "false") {
    store->SetBool(setting_name, false);
  } else if (value == "1" || value == "true") {
    store->SetBool(setting_name, true);
  } else {
    return Err("%s expects a boolean. See \"help set\" for valid values.", setting_name.data());
  }
  return Err();
}

Err SetInt(SettingStore* store, const std::string& setting_name, const std::string& value) {
  int out;
  Err err = StringToInt(value, &out);
  if (err.has_error()) {
    return Err("%s expects a valid int: %s", setting_name.data(), err.msg().data());
  }

  return store->SetInt(setting_name, out);
}

Err SetList(const SettingContext& setting_context, const std::vector<std::string>& values,
            SettingStore* store) {
  if (setting_context.op == ParsedSetCommand::kAssign)
    return store->SetList(setting_context.setting.info.name, values);

  if (setting_context.op == ParsedSetCommand::kAppend) {
    auto list = store->GetList(setting_context.setting.info.name);
    list.insert(list.end(), values.begin(), values.end());
    return store->SetList(setting_context.setting.info.name, list);
  }

  if (setting_context.op == ParsedSetCommand::kRemove) {
    // Search for the elements to remove.
    auto list = store->GetList(setting_context.setting.info.name);

    for (const std::string& value : values) {
      auto first_to_remove = std::remove(list.begin(), list.end(), value);
      if (first_to_remove == list.end()) {
        // Item not found.
        return Err("Could not find \"" + value + "\" to remove from setting list.");
      }
      list.erase(first_to_remove, list.end());
    }
    return store->SetList(setting_context.setting.info.name, list);
  }

  FXL_NOTREACHED();
  return Err();
}

Err SetExecutionScope(ConsoleContext* console_context, const SettingContext& setting_context,
                      const std::string& scope_str, SettingStore* store) {
  ErrOr<ExecutionScope> scope_or = ParseExecutionScope(console_context, scope_str);
  if (scope_or.has_error())
    return scope_or.err();

  return store->SetExecutionScope(setting_context.setting.info.name, scope_or.value());
}

// Will run the sets against the correct SettingStore:
// |setting_context| represents the required context needed to reason about the command.
// for user feedback.
// |out| is the resultant setting, which is used for user feedback.
Err SetSetting(ConsoleContext* console_context, const SettingContext& setting_context,
               const std::vector<std::string>& values, SettingStore* store, Setting* out) {
  Err err;
  if (setting_context.op != ParsedSetCommand::kAssign && !setting_context.setting.value.is_list())
    return Err("Appending/removing only works for list options.");

  switch (setting_context.setting.value.type()) {
    case SettingType::kBoolean:
      err = SetBool(store, setting_context.setting.info.name, values[0]);
      break;
    case SettingType::kInteger:
      err = SetInt(store, setting_context.setting.info.name, values[0]);
      break;
    case SettingType::kString:
      err = store->SetString(setting_context.setting.info.name, values[0]);
      break;
    case SettingType::kList:
      err = SetList(setting_context, values, store);
      break;
    case SettingType::kExecutionScope:
      err = SetExecutionScope(console_context, setting_context, values[0], store);
      break;
    case SettingType::kNull:
      return Err("Unknown type for setting %s. Please file a bug with repro.",
                 setting_context.setting.info.name.data());
  }

  if (!err.ok())
    return err;

  *out = store->GetSetting(setting_context.setting.info.name);
  return Err();
}

OutputBuffer FormatSetFeedback(ConsoleContext* console_context,
                               const SettingContext& setting_context,
                               const std::string& setting_name, const Command& cmd,
                               const Setting& setting) {
  OutputBuffer out;
  out.Append("New value ");
  out.Append(Syntax::kVariable, setting_name);

  std::string message;
  switch (setting_context.level) {
    case SettingContext::Level::kGlobal:
      out.Append(" system-wide:\n");
      break;
    case SettingContext::Level::kJob: {
      int job_id = console_context->IdForJobContext(cmd.job_context());
      out.Append(fxl::StringPrintf(" for job %d:\n", job_id));
      break;
    }
    case SettingContext::Level::kTarget: {
      int target_id = console_context->IdForTarget(cmd.target());
      out.Append(fxl::StringPrintf(" for process %d:\n", target_id));
      break;
    }
    case SettingContext::Level::kThread: {
      int target_id = console_context->IdForTarget(cmd.target());
      int thread_id = console_context->IdForThread(cmd.thread());
      out.Append(fxl::StringPrintf(" for thread %d of process %d:\n", thread_id, target_id));
      break;
    }
    default:
      FXL_NOTREACHED() << "Should not receive a default setting.";
  }

  out.Append(FormatSettingShort(console_context, setting));
  return out;
}

Err DoSet(ConsoleContext* console_context, const Command& cmd) {
  // The command parser will provide everything as one argument.
  if (cmd.args().size() != 1)
    return Err("Wrong amount of Arguments. See \"help set\".");

  ErrOr<ParsedSetCommand> parsed = ParseSetCommand(cmd.args()[0]);
  if (parsed.has_error())
    return parsed.err();

  // See where this setting would be stored.
  SettingContext setting_context;
  Err err = GetSettingContext(console_context, cmd, parsed.value().name, &setting_context);
  if (err.has_error())
    return err;
  setting_context.op = parsed.value().op;

  // Validate that the operations makes sense.
  if (parsed.value().op != ParsedSetCommand::kAssign && !setting_context.setting.value.is_list())
    return Err("List modification (+=, -=) used on a non-list option.");

  if (parsed.value().values.size() > 1u && !setting_context.setting.value.is_list()) {
    return Err(
        "Multiple values on a non-list option. Use \"quotes\" to include literal whitespace.");
  }

  Setting out_setting;  // Used for showing the new value.
  err = SetSetting(console_context, setting_context, parsed.value().values, setting_context.store,
                   &out_setting);
  if (!err.ok())
    return err;

  Console::get()->Output(
      FormatSetFeedback(console_context, setting_context, parsed.value().name, cmd, out_setting));
  return Err();
}

// Equivalend to isalpha with no C local complexity.
bool IsSettingNameAlpha(const char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// Setting names are only letters and hyphens.
//
// The "name" is annoying to parse because it can't end in a "-" and we have to differentiate that
// from a "name-=..." command which requires lookahead. As a result, this takes the full string
// and the index of the character to check.
bool IsSettingNameChar(const std::string& input, size_t i) {
  if (IsSettingNameAlpha(input[i]))
    return true;  // Normal letter.

  if (input[i] == '-') {
    // Disambiguate internal hyphens from "-=".
    if (i + 1 < input.size() && IsSettingNameAlpha(input[i + 1]))
      return true;  // More name to come.
  }

  return false;
}

void CompleteSet(const Command& cmd, const std::string& prefix,
                 std::vector<std::string>* completions) {
  // Find the end of the setting name. The argument is a whole expression as one string, so if we're
  // past the setting name we're past the part we know how to complete.
  size_t cur;
  for (cur = 0; cur < prefix.size() && IsSettingNameChar(prefix, cur); ++cur)
    ;

  if (cur == prefix.size()) {
    for (const auto& [name, _] : System::GetSchema()->settings()) {
      if (!name.compare(0, prefix.size(), prefix)) {
        completions->push_back(name);
      }
    }

    for (const auto& [name, _] : Target::GetSchema()->settings()) {
      if (!name.compare(0, prefix.size(), prefix)) {
        completions->push_back(name);
      }
    }

    for (const auto& [name, _] : Thread::GetSchema()->settings()) {
      if (!name.compare(0, prefix.size(), prefix)) {
        completions->push_back(name);
      }
    }

    // Jobs don't store their schema yet since it's empty. We'll have to update this code if they
    // ever do.
  }

  // TODO: We need to refactor parsing a bit if we want to complete options.
}

}  // namespace

// Grammar:
//   command := <name> [ <whitespace> ] [ <operator> <whitespace> ] <value> *
//   name := ('A' - 'Z') | ('a' - 'z') | '-'  // See IsSettingNameChar() for qualifications.
//   operator := "=" | '+=' | '-='
//
// A "value" is a possibly quoted string parsed as a command token. The whole thing can't be parsed
// as a command because we want to handle "name value" as well as "name = value" and "name=value".
ErrOr<ParsedSetCommand> ParseSetCommand(const std::string& input) {
  ParsedSetCommand result;
  size_t cur = 0;

  // Name.
  while (cur < input.size() && IsSettingNameChar(input, cur)) {
    result.name.push_back(input[cur]);
    ++cur;
  }
  if (result.name.empty())
    return Err("Expected a setting name to set.");

  // Require whitespace or an operator after the name to prevent "name$" from being valid.
  bool has_name_terminator = false;

  // Whitespace.
  while (cur < input.size() && isspace(input[cur])) {
    has_name_terminator = true;
    ++cur;
  }

  // Special-case the error message for no value to be more helpful.
  if (cur == input.size())
    return Err("Expecting a value to set. Use \"set setting-name setting-value\".");

  // Optional operator.
  if (cur < input.size() && input[cur] == '=') {
    has_name_terminator = true;
    result.op = ParsedSetCommand::kAssign;
    ++cur;
  } else if (cur < input.size() - 1 && input[cur] == '+' && input[cur + 1] == '=') {
    has_name_terminator = true;
    result.op = ParsedSetCommand::kAppend;
    cur += 2;
  } else if (cur < input.size() - 1 && input[cur] == '-' && input[cur + 1] == '=') {
    has_name_terminator = true;
    result.op = ParsedSetCommand::kRemove;
    cur += 2;
  }

  // Check for the name terminator.
  if (!has_name_terminator)
    return Err("Invalid setting name.");

  // Whitespace.
  while (cur < input.size() && isspace(input[cur]))
    ++cur;

  // Value(s) parsed as a command token. This handles the various types of escaping and quoting
  // supported by the interactive command line.
  std::vector<CommandToken> value_tokens;
  if (Err err = TokenizeCommand(input.substr(cur), &value_tokens); err.has_error())
    return err;
  if (value_tokens.empty())
    return Err("Expected a value to set.");

  for (const auto& token : value_tokens)
    result.values.push_back(std::move(token.str));
  return result;
}

ErrOr<ExecutionScope> ParseExecutionScope(ConsoleContext* console_context,
                                          const std::string& input) {
  // Use the command parser to get the scope.
  Command parsed;
  if (Err err = ParseCommand(input, &parsed); err.has_error())
    return err;

  const char kScopeHelp[] =
      "Scope not valid, expecting \"global\", \"process\", or \"thread\" only.";

  // We accept the "global", "process", and "thread" nouns, and no verbs or switches.
  if (parsed.verb() != Verb::kNone)
    return Err(kScopeHelp);
  if (parsed.ValidateNouns({Noun::kGlobal, Noun::kProcess, Noun::kThread}).has_error())
    return Err(kScopeHelp);
  if (!parsed.args().empty())
    return Err(kScopeHelp);

  // Convert the nouns to objects.
  if (Err err = console_context->FillOutCommand(&parsed); err.has_error())
    return err;

  // Valid, convert to scope based on what nouns were specified.
  if (parsed.HasNoun(Noun::kThread))
    return ExecutionScope(parsed.thread());
  if (parsed.HasNoun(Noun::kProcess))
    return ExecutionScope(parsed.target());
  return ExecutionScope();
}

void AppendSettingsVerbs(std::map<Verb, VerbRecord>* verbs) {
  VerbRecord get(&DoGet, {"get"}, kGetShortHelp, kGetHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kGet] = std::move(get);

  VerbRecord set(&DoSet, &CompleteSet, {"set"}, kSetShortHelp, kSetHelp, CommandGroup::kGeneral);
  set.param_type = VerbRecord::kOneParam;
  (*verbs)[Verb::kSet] = std::move(set);
}

}  // namespace zxdb
