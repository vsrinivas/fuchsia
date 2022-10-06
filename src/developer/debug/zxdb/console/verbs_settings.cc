// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/verbs_settings.h"

#include <ctype.h>

#include <algorithm>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/execution_scope.h"
#include "src/developer/debug/zxdb/client/filter.h"
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
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/nouns.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kValueOnlySwitch = 1;

// TODO(brettw) Some of this is very repetitive. The Check*() functions and the big block in
// GetSettingContext() duplicate getting scopes and checking stuff.
//
// It would be nice if this was expressed in more of a table form to eliminate this code. The
// Level enum could also be removed if the setting store description could be added to the
// SettingContext.

// Struct to represents all the context needed to correctly reason about the settings commands.
struct SettingContext {
  enum class Level {
    kGlobal,
    kTarget,
    kThread,
    kBreakpoint,
    kFilter,
  };

  // The SettingStore we're using. This will either be the one the user specified or the implicit
  // one deteched for the value. It will be null if the name is empty and there was no explicitly
  // specified one.
  SettingStore* store = nullptr;

  // Set when the user has explicitly set the setting store ("thread 1 set ..."). False means the
  // store was determined implicitly based on the name.
  bool explicit_store = false;

  std::string name;
  SettingValue value;

  // At what level the setting was applied.
  Level level = Level::kGlobal;

  // What kind of operation this is for set commands.
  ParsedSetCommand::Operation op = ParsedSetCommand::kAssign;
};

// Fixes up user typing for a setting name by lower-casing.
std::string CanonicalizeSettingName(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (char c : input)
    output.push_back(tolower(c));
  return output;
}

bool CheckGlobal(ConsoleContext* context, const Command& cmd, const std::string& setting_name,
                 SettingContext* out) {
  if (!context->session()->system().settings().schema()->HasSetting(setting_name))
    return false;
  out->store = &context->session()->system().settings();
  out->level = SettingContext::Level::kGlobal;
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

  out->name = CanonicalizeSettingName(setting_name);

  // Handle noun overrides for getting/setting on specific objects.
  // TODO(brettw) see above for TODO about reducing this duplication.
  if (cmd.HasNoun(Noun::kThread)) {
    out->store = cmd.thread() ? &cmd.thread()->settings() : nullptr;
    out->explicit_store = true;
    out->level = SettingContext::Level::kThread;
  } else if (cmd.HasNoun(Noun::kProcess)) {
    out->store = &cmd.target()->settings();
    out->explicit_store = true;
    out->level = SettingContext::Level::kTarget;
  } else if (cmd.HasNoun(Noun::kGlobal)) {
    out->store = &context->session()->system().settings();
    out->explicit_store = true;
    out->level = SettingContext::Level::kGlobal;
  } else if (cmd.HasNoun(Noun::kBreakpoint)) {
    out->store = cmd.breakpoint() ? &cmd.breakpoint()->settings() : nullptr;
    out->explicit_store = true;
    out->level = SettingContext::Level::kBreakpoint;
  } else if (cmd.HasNoun(Noun::kFilter)) {
    out->store = cmd.filter() ? &cmd.filter()->settings() : nullptr;
    out->explicit_store = true;
    out->level = SettingContext::Level::kFilter;
  }

  // When no name is specified, there's no implicit setting store to look up.
  if (out->name.empty())
    return Err();

  if (out->store) {
    // Found an explicitly requested setting store.
    if (cmd.verb() == Verb::kSet) {
      // Use the generic definition from the schama (if any).
      if (const SettingSchema::Record* record = out->store->schema()->GetSetting(out->name))
        out->value = record->default_value;
    } else if (cmd.verb() == Verb::kGet) {
      // Use the specific value from the store.
      out->value = out->store->GetValue(out->name);
    }
    return Err();
  }

  // Didn't found an explicit specified store, so lookup in the current context. Since the
  // settings can be duplicated on different levels, we need to search in the order that makes
  // sense for the command.
  out->explicit_store = false;

  // This array encodes the order we search from the most global to most specific store. This is
  // not the same as the SettingStore fallback because "set" searches from global->specific ("get"
  // does the reverse).
  using CheckFunction =
      bool (*)(ConsoleContext*, const Command&, const std::string&, SettingContext*);
  const CheckFunction kGlobalToSpecific[] = {
      &CheckGlobal,
      &CheckTarget,
      &CheckThread,
  };

  if (cmd.verb() == Verb::kSet) {
    // When setting, choose the most global context the setting can apply to.
    for (const auto cur : kGlobalToSpecific) {
      if (cur(context, cmd, out->name, out)) {
        // Just get the default value so the type is set properly.
        out->value = out->store->schema()->GetSetting(out->name)->default_value;
        return Err();
      }
    }
  } else if (cmd.verb() == Verb::kGet) {
    // When getting, choose the most specific context the setting can apply to.
    for (const auto cur : Reversed(kGlobalToSpecific)) {
      if (cur(context, cmd, out->name, out)) {
        // Getting additionally requires that the setting be non-null. We want to find the first
        // one that might apply.
        out->value = out->store->GetValue(out->name);
        if (!out->value.is_null())
          return Err();
      }
    }
  }

  return Err("Could not find setting \"%s\".", out->name.data());
}

// get ---------------------------------------------------------------------------------------------

const char kGetShortHelp[] = "get: Prints setting values.";
const char kGetHelp[] =
    R"([ <object> ] get [ --value-only ] [ <setting-name> ]

  Prints setting values.

  Settings are hierarchical for processes and threads. This means that
  thread settings will inherit from the process if they don't exist on
  the thread, and process settings will inherit from the global settings
  if they don't exist on the process.

get

    By itself, "get" will show the global settings and the ones for the
    current thread and process.

    Example:
      get

get <setting-name>

    Prints the value of the named setting. It will search the current
    thread, followed by the current process, and then global settings.

    Examples:
      get build-dirs
      get show-stdout

<object> get

    Prints all settings from a specific object.

    Examples:
      process 1 thread 1 get
      thread get              // Uses the current thread.
      filter 2 get
      global get              // Explicitly requests only global values
                              // (processes and threads may override).

<object> get <setting-name>

    Prints a named setting from a specific object.

    Examples:
      filter 2 get pattern
      filter get pattern     // Uses the current filter.

Options

  --value-only
      Displays only the value of the setting without any help text or
      formatting.
)";

// Outputs the general settings in the following order: System -> Target -> Thread
Err CompleteSettingsToOutput(const Command& cmd, ConsoleContext* context, OutputBuffer* out) {
  out->Append(OutputBuffer(Syntax::kHeading, "Global\n"));
  out->Append(FormatSettingStore(context, context->session()->system().settings()));
  out->Append("\n");

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

void DoGet(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  std::string input_name;
  if (!cmd.args().empty()) {
    if (cmd.args().size() > 1)
      return cmd_context->ReportError(Err("Expected only one setting name"));
    input_name = cmd.args()[0];
  }

  Err err;
  SettingContext setting_context;
  if (err = GetSettingContext(console_context, cmd, input_name, &setting_context); err.has_error())
    return cmd_context->ReportError(err);
  // Use setting_context.name from here on down instead of input_name because it's canonicalized.

  OutputBuffer out;
  if (!setting_context.name.empty()) {
    // Getting one setting.
    if (setting_context.value.is_null()) {
      err = Err("Could not find setting %s", setting_context.name.c_str());
    } else if (cmd.HasSwitch(kValueOnlySwitch)) {
      out = FormatSettingValue(console_context, setting_context.value);
    } else {
      const SettingSchema::Record* record =
          setting_context.store->schema()->GetSetting(setting_context.name);
      FX_DCHECK(record);  // Should always succeed if GetSettingContext did.
      out = FormatSetting(console_context, setting_context.name, record->description,
                          setting_context.value);
    }
  } else if (setting_context.explicit_store) {
    // Getting all settings on one object. The object may not be valid (e.g. "thread get" when
    // there's no current thread).
    if (setting_context.store) {
      out.Append(FormatSettingStore(console_context, *setting_context.store));
      out.Append("\n");
    } else {
      err = Err("No current object of this type.");
    }
  } else {
    // Implcitly getting all settings, show the general global/process/thread ones.
    err = CompleteSettingsToOutput(cmd, console_context, &out);
  }

  if (err.has_error())
    return cmd_context->ReportError(err);
  cmd_context->Output(out);
}

// set ---------------------------------------------------------------------------------------------

const char kSetShortHelp[] = "set: Set a setting value.";
const char kSetHelp[] =
    R"([ <object> ] set <setting-name> [ <modification-type> ] <value>*

  Sets the value of a setting.

  See which settings are available, their names and current values with
  the "get" command (see "help get").

  As a special-case, the syntax "set <setting-name> =" (with no value) will
  clear the setting back to its default value. This can also be used for thread-
  or process-specific settings to case a fallback to the global value.

Arguments

  <object>
      The object that the setting is on. If unspecified, it will search
      the current thread, process, and then the global settings for a
      match to set.

  <setting_name>
      The setting that will modified. Case-insensitive.

  <modification-type>
      Operator that indicates how to mutate a list. For non-lists only = (the
      default) is supported:

      =   Replace the current contents (the default).
      +=  Append the given value to the list.
      -=  Search for the given value and remove it.

  <value>
      The value(s) to set, add, or remove. Multiple values for list settings
      are whitespace separated. You can "quote" strings to include literal
      spaces, and backslash-escape literal quotes.

Setting Types

  Each setting has a particular type.

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
  Set global show-stdout = true

  [zxdb] pr 3 set vector-format double
  Set process 3 vector-format = double

  [zxdb] get build-dirs
    • /home/me/build
    • /home/me/other/out

  [zxdb] set build-dirs += /tmp/build
  Set global build-dirs =
    • /home/me/build
    • /home/me/other/out
    • /tmp/build

  [zxdb] set build-dirs = /other/build/location /other/build2
  Set global build-dirs build-dirs =
    • /other/build/location
    • /other/build2

  [zxdb] set build-dirs =
  Set global build-dirs = <empty>
)";

Err SetBool(SettingStore* store, const std::string& setting_name, const std::string& value) {
  std::optional<bool> bool_val = StringToBool(value);
  if (!bool_val)
    return Err("%s expects a boolean. See \"help set\" for valid values.", setting_name.data());

  store->SetBool(setting_name, *bool_val);
  return Err();
}

Err SetInt(SettingStore* store, const std::string& setting_name, const std::string& value) {
  int64_t out;
  Err err = StringToInt64(value, &out);
  if (err.has_error()) {
    return Err("%s expects a valid int: %s", setting_name.data(), err.msg().data());
  }

  return store->SetInt(setting_name, out);
}

Err SetList(const SettingContext& setting_context, const std::vector<std::string>& values,
            SettingStore* store) {
  if (setting_context.op == ParsedSetCommand::kAssign)
    return store->SetList(setting_context.name, values);

  if (setting_context.op == ParsedSetCommand::kAppend) {
    auto list = store->GetList(setting_context.name);
    list.insert(list.end(), values.begin(), values.end());
    return store->SetList(setting_context.name, list);
  }

  if (setting_context.op == ParsedSetCommand::kRemove) {
    // Search for the elements to remove.
    auto list = store->GetList(setting_context.name);

    for (const std::string& value : values) {
      auto first_to_remove = std::remove(list.begin(), list.end(), value);
      if (first_to_remove == list.end()) {
        // Item not found.
        return Err("Could not find \"" + value + "\" to remove from setting list.");
      }
      list.erase(first_to_remove, list.end());
    }
    return store->SetList(setting_context.name, list);
  }

  FX_NOTREACHED();
  return Err();
}

Err SetExecutionScope(ConsoleContext* console_context, const SettingContext& setting_context,
                      const std::string& scope_str, SettingStore* store) {
  ErrOr<ExecutionScope> scope_or = ParseExecutionScope(console_context, scope_str);
  if (scope_or.has_error())
    return scope_or.err();

  return store->SetExecutionScope(setting_context.name, scope_or.value());
}

Err SetInputLocations(const Frame* optional_frame, const SettingContext& setting_context,
                      const std::string& input, SettingStore* store) {
  std::vector<InputLocation> locs;
  if (Err err = ParseLocalInputLocation(optional_frame, input, &locs); err.has_error())
    return err;

  return store->SetInputLocations(setting_context.name, std::move(locs));
}

// Will run the sets against the correct SettingStore:
// |setting_context| represents the required context needed to reason about the command.
// for user feedback.
// |out| is the resultant setting, which is used for user feedback.
Err SetSetting(ConsoleContext* console_context, const Frame* optional_frame,
               const SettingContext& setting_context, const std::vector<std::string>& values,
               SettingStore* store, SettingValue* out) {
  Err err;
  if (setting_context.op != ParsedSetCommand::kAssign && !setting_context.value.is_list())
    return Err("Appending/removing only works for list options.");

  if (values.empty()) {
    if (err = store->ClearValue(setting_context.name); err.has_error())
      return err;

    // When clearing, report the new value is the default reported by the store.
    *out = store->GetValue(setting_context.name);
    return Err();
  }

  switch (setting_context.value.type()) {
    case SettingType::kBoolean:
      err = SetBool(store, setting_context.name, values[0]);
      break;
    case SettingType::kInteger:
      err = SetInt(store, setting_context.name, values[0]);
      break;
    case SettingType::kString:
      err = store->SetString(setting_context.name, values[0]);
      break;
    case SettingType::kList:
      err = SetList(setting_context, values, store);
      break;
    case SettingType::kExecutionScope:
      err = SetExecutionScope(console_context, setting_context, values[0], store);
      break;
    case SettingType::kInputLocations:
      err = SetInputLocations(optional_frame, setting_context, values[0], store);
      break;
    case SettingType::kNull:
      return Err("Could not find setting %s.", setting_context.name.c_str());
  }

  if (!err.ok())
    return err;

  *out = store->GetValue(setting_context.name);
  return Err();
}

OutputBuffer FormatSetFeedback(ConsoleContext* console_context,
                               const SettingContext& setting_context,
                               const std::string& setting_name, const Command& cmd,
                               const SettingValue& value) {
  OutputBuffer out;

  std::string message;
  switch (setting_context.level) {
    case SettingContext::Level::kGlobal:
      out.Append("Set global");
      break;
    case SettingContext::Level::kTarget:
      out.Append("Set process ");
      out.Append(Syntax::kSpecial, std::to_string(console_context->IdForTarget(cmd.target())));
      break;
    case SettingContext::Level::kThread:
      out.Append("Set process ");
      out.Append(Syntax::kSpecial, std::to_string(console_context->IdForTarget(cmd.target())));
      out.Append(" thread ");
      out.Append(Syntax::kSpecial, std::to_string(console_context->IdForThread(cmd.thread())));
      break;
    case SettingContext::Level::kBreakpoint:
      out.Append("Set breakpoint ");
      out.Append(Syntax::kSpecial,
                 std::to_string(console_context->IdForBreakpoint(cmd.breakpoint())));
      break;
    case SettingContext::Level::kFilter:
      out.Append("Set filter ");
      out.Append(Syntax::kSpecial, std::to_string(console_context->IdForFilter(cmd.filter())));
      break;
    default:
      FX_NOTREACHED() << "Should not receive a default setting.";
  }

  out.Append(" ");
  out.Append(Syntax::kVariable, setting_name);
  out.Append(" = ");

  if (value.is_list() && !value.get_list().empty())
    out.Append("\n");  // Put list items on their own lines.
  out.Append(FormatSettingShort(console_context, setting_name, value, 2));
  return out;
}

void DoSet(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  // The command parser will provide everything as one argument.
  if (cmd.args().size() != 1) {
    return cmd_context->ReportError(
        Err("Expected a setting and a new value.\n"
            " • Type \"help set\" for usage.\n"
            " • Type \"get\" to list all settings and their current values.\n"
            " • Type \"get <setting-name>\" for documentation on a setting."));
  }

  ErrOr<ParsedSetCommand> parsed = ParseSetCommand(cmd.args()[0]);
  if (parsed.has_error())
    return cmd_context->ReportError(parsed.err());

  // See where this setting would be stored.
  SettingContext setting_context;
  Err err = GetSettingContext(console_context, cmd, parsed.value().name, &setting_context);
  if (err.has_error())
    return cmd_context->ReportError(err);
  setting_context.op = parsed.value().op;

  // Validate that the operations makes sense.
  if (parsed.value().op != ParsedSetCommand::kAssign && !setting_context.value.is_list())
    return cmd_context->ReportError(Err("List modification (+=, -=) used on a non-list option."));

  if (parsed.value().values.size() > 1u && !setting_context.value.is_list()) {
    // When the value is a non-list, assume input with spaces is a single literal. This allows
    // input like:
    //    bp set scope process 1
    // without quoting "process 1" which is much more intuitive. The potentially surprising side
    // effect of this rule is that if two things are quoted (triggering this condition), the quotes
    // will be literally included in the value, while if only one thing is quoted, the quotes will
    // br trimmed. But this edge case is not worth adding extra complexity here.
    parsed.value().values.clear();
    parsed.value().values.push_back(parsed.value().raw_value);
  }

  SettingValue out_value;  // Used for showing the new value.
  err = SetSetting(console_context, cmd.frame(), setting_context, parsed.value().values,
                   setting_context.store, &out_value);
  if (!err.ok())
    return cmd_context->ReportError(err);

  // Be sure to use the canonicalized name in the setting_context for the output.
  cmd_context->Output(
      FormatSetFeedback(console_context, setting_context, setting_context.name, cmd, out_value));
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
  }

  // TODO: We need to refactor parsing a bit if we want to complete options.
}

}  // namespace

// Grammar:
//   command := <name> [ <whitespace> ] [ <operator> <whitespace> ] [ <value> * ]
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

  // Special-case the error message for no value to be more helpful. To clear the value we require
  // "=" so the command is more explicit ("set foo" may be typed by somebody who doesn't know what
  // they're doing).
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

  // Save the original raw value (trimming whitespace from the right).
  result.raw_value = input.substr(cur);
  while (!result.raw_value.empty() && isspace(result.raw_value.back()))
    result.raw_value.resize(result.raw_value.size() - 1);

  // Value(s) parsed as a command token. This handles the various types of escaping and quoting
  // supported by the interactive command line.
  //
  // The value can be omitted for sets which clears the value to the default.
  std::vector<CommandToken> value_tokens;
  if (Err err = TokenizeCommand(input.substr(cur), &value_tokens); err.has_error())
    return err;
  if (result.op != ParsedSetCommand::kAssign && value_tokens.empty())
    return Err("Expected a value to add/remove.");

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

  // Valid, convert to scope based on what nouns were specified. The thread or process could have
  // failed to resolve to objects if there is no current one.
  if (parsed.HasNoun(Noun::kThread)) {
    if (!parsed.thread())
      return Err("There is no current thread to use for the scope.");
    return ExecutionScope(parsed.thread());
  }
  if (parsed.HasNoun(Noun::kProcess)) {
    if (!parsed.target())
      return Err("There is no current process to use for the scope.");
    return ExecutionScope(parsed.target());
  }
  return ExecutionScope();
}

void AppendSettingsVerbs(std::map<Verb, VerbRecord>* verbs) {
  VerbRecord get(&DoGet, {"get"}, kGetShortHelp, kGetHelp, CommandGroup::kGeneral);
  get.switches.emplace_back(kValueOnlySwitch, false, "value-only");
  (*verbs)[Verb::kGet] = std::move(get);

  VerbRecord set(&DoSet, &CompleteSet, {"set"}, kSetShortHelp, kSetHelp, CommandGroup::kGeneral);
  set.param_type = VerbRecord::kOneParam;
  (*verbs)[Verb::kSet] = std::move(set);
}

}  // namespace zxdb
