// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command.h"

#include <algorithm>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/nouns.h"
#include "garnet/bin/zxdb/console/verbs.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

const int Command::kNoIndex;

Command::Command() = default;
Command::~Command() = default;

bool Command::HasNoun(Noun noun) const {
  return nouns_.find(noun) != nouns_.end();
}

int Command::GetNounIndex(Noun noun) const {
  auto found = nouns_.find(noun);
  if (found == nouns_.end())
    return kNoIndex;
  return found->second;
}

void Command::SetNoun(Noun noun, int index) {
  FXL_DCHECK(nouns_.find(noun) == nouns_.end());
  nouns_[noun] = index;
}

Err Command::ValidateNouns(std::initializer_list<Noun> allowed_nouns) const {
  for (const auto& pair : nouns_) {
    if (std::find(allowed_nouns.begin(), allowed_nouns.end(), pair.first) ==
        allowed_nouns.end()) {
      return Err(
          ErrType::kInput,
          fxl::StringPrintf("\"%s\" may not be specified for this command.",
                            NounToString(pair.first).c_str()));
    }
  }
  return Err();
}

bool Command::HasSwitch(int id) const {
  return switches_.find(id) != switches_.end();
}

std::string Command::GetSwitchValue(int id) const {
  auto found = switches_.find(id);
  if (found == switches_.end())
    return std::string();
  return found->second;
}

void Command::SetSwitch(int id, std::string str) {
  switches_[id] = std::move(str);
}

SwitchRecord::SwitchRecord() = default;
SwitchRecord::SwitchRecord(const SwitchRecord&) = default;
SwitchRecord::SwitchRecord(int i, bool has_value, const char* n, char c)
    : id(i), has_value(has_value), name(n), ch(c) {}
SwitchRecord::~SwitchRecord() = default;

NounRecord::NounRecord() = default;
NounRecord::NounRecord(std::initializer_list<std::string> aliases,
                       const char* short_help, const char* help,
                       CommandGroup command_group)
    : aliases(aliases),
      short_help(short_help),
      help(help),
      command_group(command_group) {}
NounRecord::~NounRecord() = default;

VerbRecord::VerbRecord() = default;
VerbRecord::VerbRecord(CommandExecutor exec,
                       std::initializer_list<std::string> aliases,
                       const char* short_help, const char* help,
                       CommandGroup command_group,
                       SourceAffinity source_affinity)
    : exec(exec),
      aliases(aliases),
      short_help(short_help),
      help(help),
      command_group(command_group),
      source_affinity(source_affinity) {}
VerbRecord::VerbRecord(CommandExecutorWithCallback exec_cb,
                       std::initializer_list<std::string> aliases,
                       const char* short_help, const char* help,
                       CommandGroup command_group,
                       SourceAffinity source_affinity)
    : exec_cb(exec_cb),
      aliases(aliases),
      short_help(short_help),
      help(help),
      command_group(command_group),
      source_affinity(source_affinity) {}
VerbRecord::~VerbRecord() = default;

std::string NounToString(Noun n) {
  const auto& nouns = GetNouns();
  auto found = nouns.find(n);
  if (found == nouns.end())
    return std::string();
  return found->second.aliases[0];
}

std::string VerbToString(Verb v) {
  const auto& verbs = GetVerbs();
  auto found = verbs.find(v);
  if (found == verbs.end())
    return std::string();
  return found->second.aliases[0];
}

const std::map<Noun, NounRecord>& GetNouns() {
  static std::map<Noun, NounRecord> all_nouns;
  if (all_nouns.empty()) {
    AppendNouns(&all_nouns);

    // Everything but Noun::kNone (= 0) should be in the map.
    FXL_DCHECK(all_nouns.size() == static_cast<size_t>(Noun::kLast) - 1)
        << "You need to update the noun lookup table for additions to Nouns.";
  }
  return all_nouns;
}

const std::map<Verb, VerbRecord>& GetVerbs() {
  static std::map<Verb, VerbRecord> all_verbs;
  if (all_verbs.empty()) {
    AppendBreakpointVerbs(&all_verbs);
    AppendControlVerbs(&all_verbs);
    AppendMemoryVerbs(&all_verbs);
    AppendProcessVerbs(&all_verbs);
    AppendSymbolVerbs(&all_verbs);
    AppendSystemVerbs(&all_verbs);
    AppendThreadVerbs(&all_verbs);

    // Everything but Noun::kNone (= 0) should be in the map.
    FXL_DCHECK(all_verbs.size() == static_cast<size_t>(Verb::kLast) - 1)
        << "You need to update the verb lookup table for additions to Verbs.";
  }
  return all_verbs;
}

const VerbRecord* GetVerbRecord(Verb verb) {
  const auto& verbs = GetVerbs();
  auto found = verbs.find(verb);
  if (found == verbs.end())
    return nullptr;
  return &found->second;
}

const std::map<std::string, Noun>& GetStringNounMap() {
  static std::map<std::string, Noun> map;
  if (map.empty()) {
    // Build up the reverse-mapping from alias to verb enum.
    for (const auto& noun_pair : GetNouns()) {
      for (const auto& alias : noun_pair.second.aliases)
        map[alias] = noun_pair.first;
    }
  }
  return map;
}

const std::map<std::string, Verb>& GetStringVerbMap() {
  static std::map<std::string, Verb> map;
  if (map.empty()) {
    // Build up the reverse-mapping from alias to verb enum.
    for (const auto& verb_pair : GetVerbs()) {
      for (const auto& alias : verb_pair.second.aliases)
        map[alias] = verb_pair.first;
    }
  }
  return map;
}

Err DispatchCommand(ConsoleContext* context, const Command& cmd,
                    CommandCallback callback) {
  if (cmd.verb() == Verb::kNone)
    return ExecuteNoun(context, cmd);

  const auto& verbs = GetVerbs();
  const auto& found = verbs.find(cmd.verb());
  if (found == verbs.end()) {
    return Err(ErrType::kInput,
               "Invalid verb \"" + VerbToString(cmd.verb()) + "\".");
  }

  auto& verb_record = found->second;
  if (verb_record.exec_cb) {
    return verb_record.exec_cb(context, cmd, callback);
  } else {
    Err original_err = verb_record.exec(context, cmd);
    if (callback) {
      // We need to call the callback to let the caller know they ran a command
      // that doesn't receive callbacks.
      Err callback_err =
          original_err.has_error()
              ? original_err
              : Err("Command was processed but it doesn't receive "
                    "callbacks. Going to interactive mode.");
      // Commands without callbacks never quit by callback.
      callback(callback_err);
    }
    return original_err;
  }
}

}  // namespace zxdb
