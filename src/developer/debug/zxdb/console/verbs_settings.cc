// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/verbs.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_settings.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

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

Err SettingToOutput(const Command& cmd, const std::string& key,
                    ConsoleContext* context, OutputBuffer* out) {
  // We search in the following order: Thread -> Target -> Job -> System.
  if (Thread* thread = cmd.thread()) {
    Setting setting = thread->settings().GetSetting(key);
    if (!setting.value.is_null()) {
      *out = FormatSetting(setting);
      return Err();
    }
  }

  if (Target* target = cmd.target()) {
    Setting setting = target->settings().GetSetting(key);
    if (!setting.value.is_null()) {
      *out = FormatSetting(setting);
      return Err();
    }
  }

  if (JobContext* job_context = cmd.job_context()) {
    Setting setting = job_context->settings().GetSetting(key);
    if (!setting.value.is_null()) {
      *out = FormatSetting(setting);
      return Err();
    }
  }

  Setting setting = context->session()->system().settings().GetSetting(key);
  if (!setting.value.is_null()) {
    *out = FormatSetting(setting);
    return Err();
  }

  return Err("Could not find setting %s", key.c_str());
}

Err CompleteSettingsToOutput(const Command& cmd, ConsoleContext* context,
                             OutputBuffer* out) {
  // Output in the following order: System -> Job -> Target -> Thread
  out->Append(OutputBuffer(Syntax::kHeading, "Global\n"));
  out->Append(FormatSettingStore(context->session()->system().settings()));
  out->Append("\n");

  if (JobContext* job = cmd.job_context();
      job && !job->settings().schema()->empty()) {
    auto title = fxl::StringPrintf("Job %d\n", context->IdForJobContext(job));
    out->Append(OutputBuffer(Syntax::kHeading, std::move(title)));
    out->Append(FormatSettingStore(job->settings()));
    out->Append("\n");
  }

  if (Target* target = cmd.target();
      target && !target->settings().schema()->empty()) {
    auto title = fxl::StringPrintf("Target %d\n", context->IdForTarget(target));
    out->Append(OutputBuffer(Syntax::kHeading, std::move(title)));
    out->Append(FormatSettingStore(target->settings()));
    out->Append("\n");
  }

  if (Thread* thread = cmd.thread();
      thread && !thread->settings().schema()->empty()) {
    auto title = fxl::StringPrintf("Thread %d\n", context->IdForThread(thread));
    out->Append(OutputBuffer(Syntax::kHeading, std::move(title)));
    out->Append(FormatSettingStore(thread->settings()));
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
    err = SettingToOutput(cmd, setting_name, context, &out);
  } else {
    err = CompleteSettingsToOutput(cmd, context, &out);
  }

  if (err.has_error())
    return err;
  Console::get()->Output(out);
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

// Struct to represents all the context needed to correctly reason about the
// set command.
struct SetContext {
  enum class Level {
    kGlobal,
    kJob,
    kTarget,
    kThread,
  };

  SettingStore* store = nullptr;
  JobContext* job_context = nullptr;
  Target* target = nullptr;
  Thread* thread = nullptr;

  Setting setting;  // What kind of setting this is.
  // At what level the setting was applied.
  Level level = Level::kGlobal;
  // What kind of operation this is.
  AssignType assign_type = AssignType::kAssign;

  // On append, it is the elements added.
  // On remove, it is the elements removed.
  std::vector<std::string> elements_changed;
};

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

Err SetList(const SetContext& context,
            const std::vector<std::string>& elements_to_set,
            SettingStore* store, std::vector<std::string>* elements_changed) {
  if (context.assign_type == AssignType::kAssign)
    return store->SetList(context.setting.info.name, elements_to_set);

  if (context.assign_type == AssignType::kAppend) {
    auto list = store->GetList(context.setting.info.name);
    list.insert(list.end(), elements_to_set.begin(), elements_to_set.end());
    *elements_changed = elements_to_set;
    return store->SetList(context.setting.info.name, list);
  }

  if (context.assign_type == AssignType::kRemove) {
    // We search for the elements to remove.
    auto list = store->GetList(context.setting.info.name);

    std::vector<std::string> list_after_remove;
    for (auto& elem : list) {
      // If the element to change is within the list, means that we remove it.
      auto it = std::find(elements_to_set.begin(), elements_to_set.end(), elem);
      if (it == elements_to_set.end()) {
        list_after_remove.push_back(elem);
      } else {
        elements_changed->push_back(elem);
      }
    }

    // If none, were removed, we error so that the user can check why.
    if (list.size() == list_after_remove.size())
      return Err("Could not find any elements to remove.");
    return store->SetList(context.setting.info.name, list_after_remove);
  }

  FXL_NOTREACHED();
  return Err();
}

// Will run the sets against the correct SettingStore:
// |context| represents the required context needed to reason about the command.
// |elements_changed| are all the values that changed. This is used afterwards
// for user feedback.
// |out| is the resultant setting, which is used for user feedback.
Err SetSetting(const SetContext& context,
               const std::vector<std::string>& elements_to_set,
               SettingStore* store, std::vector<std::string>* elements_changed,
               Setting* out) {
  Err err;
  if (context.assign_type != AssignType::kAssign &&
      !context.setting.value.is_list())
    return Err("Appending/removing only works for list options.");

  switch (context.setting.value.type) {
    case SettingType::kBoolean:
      err = SetBool(store, context.setting.info.name, elements_to_set[0]);
      break;
    case SettingType::kInteger:
      err = SetInt(store, context.setting.info.name, elements_to_set[0]);
      break;
    case SettingType::kString:
      err = store->SetString(context.setting.info.name, elements_to_set[0]);
      break;
    case SettingType::kList:
      err = SetList(context, elements_to_set, store, elements_changed);
      break;
    case SettingType::kNull:
      return Err("Unknown type for setting %s. Please file a bug with repro.",
                 context.setting.info.name.data());
  }

  if (!err.ok())
    return err;

  *out = store->GetSetting(context.setting.info.name);
  return Err();
}

OutputBuffer FormatSetFeedback(ConsoleContext* context,
                               const SetContext& set_context) {
  std::string verb;
  switch (set_context.assign_type) {
    case AssignType::kAssign:
      verb = "Set value(s)";
      break;
    case AssignType::kAppend:
      verb = "Added value(s)";
      break;
    case AssignType::kRemove:
      verb = "Removed the following value(s)";
      break;
  }
  FXL_DCHECK(!verb.empty());

  std::string message;
  switch (set_context.level) {
    case SetContext::Level::kGlobal:
      message = fxl::StringPrintf("%s system wide:\n", verb.data());
      break;
    case SetContext::Level::kJob: {
      int job_id = context->IdForJobContext(set_context.job_context);
      message = fxl::StringPrintf("%s for job %d:\n", verb.data(), job_id);
      break;
    }
    case SetContext::Level::kTarget: {
      int target_id = context->IdForTarget(set_context.target);
      message =
          fxl::StringPrintf("%s for process %d:\n", verb.data(), target_id);
      break;
    }
    case SetContext::Level::kThread: {
      int target_id = context->IdForTarget(set_context.target);
      int thread_id = context->IdForThread(set_context.thread);
      message = fxl::StringPrintf("%s for thread %d of process %d:\n",
                                  verb.data(), thread_id, target_id);
      break;
    }
    default:
      FXL_NOTREACHED() << "Should not receive a default setting.";
  }

  return OutputBuffer(std::move(message));
}

Err GetSetContext(const Command& cmd, const std::string& setting_name,
                  SetContext* out) {
  SettingStore* store = nullptr;
  JobContext* job_context = cmd.job_context();
  Target* target = cmd.target();
  Thread* thread = cmd.thread();

  if (!target)
    return Err("No target found. Please file a bug with a repro.");

  // If the user qualified the query, it means that we want to query *that*
  // specific SettingStore, so we search for it. We search from more specific
  // to less specific.
  if (cmd.HasNoun(Noun::kThread)) {
    store = thread ? &thread->settings() : nullptr;
    out->level = SetContext::Level::kThread;
  } else if (cmd.HasNoun(Noun::kProcess)) {
    store = &target->settings();
    out->level = SetContext::Level::kTarget;
  } else if (cmd.HasNoun(Noun::kJob)) {
    store = job_context ? &job_context->settings() : nullptr;
    out->level = SetContext::Level::kJob;
  }

  if (!store) {
    // We didn't found an explicit specified store, so we lookup in the current
    // context. We look from less specific to more specific in terms of stores,
    // so that the setting will be set for a biggest setting as it can.
    // NOTE: Some settings are replicated upwards, even to the system level,
    //       as they make sense (eg. pause on attach). But others only make
    //       sense locally (eg. job filters).
    System& system = target->session()->system();

    if (system.settings().schema()->HasSetting(setting_name)) {
      store = &system.settings();
      out->level = SetContext::Level::kGlobal;
    } else if (job_context &&
               job_context->settings().schema()->HasSetting(setting_name)) {
      store = &job_context->settings();
      out->level = SetContext::Level::kJob;
    } else if (target->settings().schema()->HasSetting(setting_name)) {
      store = &target->settings();
      out->level = SetContext::Level::kTarget;
    } else if (thread &&
               thread->settings().schema()->HasSetting(setting_name)) {
      store = &thread->settings();
      out->level = SetContext::Level::kThread;
    }
  }

  if (!store || !store->schema()->HasSetting(setting_name))
    return Err("Could not find setting \"%s\".", setting_name.data());

  out->job_context = job_context;
  out->target = target;
  out->thread = thread;
  out->store = store;
  out->setting = store->schema()->GetSetting(setting_name).setting;

  return Err();
}

Err DoSet(ConsoleContext* context, const Command& cmd) {
  if (cmd.args().size() < 2)
    return Err("Wrong amount of Arguments. See \"help set\".");

  // Expected format is <option_name> [(=|+=|-=)] <value> [<value> ...]

  Err err;
  const std::string& setting_name = cmd.args()[0];

  // See where this setting would be stored.
  SetContext set_context;
  err = GetSetContext(cmd, setting_name, &set_context);
  if (err.has_error())
    return err;

  // See what kind of assignment this is (whether it has =|+=|-=).
  AssignType assign_type;
  std::vector<std::string> elements_to_set;
  err = SetElementsToAdd(cmd.args(), &assign_type, &elements_to_set);
  if (err.has_error())
    return err;

  set_context.assign_type = assign_type;

  // Validate that the operations makes sense.
  if (assign_type != AssignType::kAssign &&
      !set_context.setting.value.is_list())
    return Err("List assignment (+=, -=) used on a non-list option.");

  if (elements_to_set.size() > 1u && !set_context.setting.value.is_list())
    return Err("Multiple values on a non-list option.");

  Setting out;  // Used for showing the new value.
  err = SetSetting(set_context, elements_to_set, set_context.store,
                   &set_context.elements_changed, &out);
  if (!err.ok())
    return err;

  // We output the new value.
  Console::get()->Output(FormatSetFeedback(context, set_context));

  // For removed values, we show which ones were removed.
  if (set_context.assign_type != AssignType::kRemove) {
    Console::get()->Output(FormatSettingShort(out));
  } else {
    Setting setting;
    setting.value = SettingValue(set_context.elements_changed);
    Console::get()->Output(FormatSettingShort(std::move(setting)));
  }

  return Err();
}

}  // namespace

void AppendSettingsVerbs(std::map<Verb, VerbRecord>* verbs) {
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
