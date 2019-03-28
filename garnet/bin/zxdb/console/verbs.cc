// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include "lib/fxl/logging.h"

namespace zxdb {

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
VerbRecord::VerbRecord(CommandExecutor exec, CommandCompleter complete,
                       std::initializer_list<std::string> aliases,
                       const char* short_help, const char* help,
                       CommandGroup command_group,
                       SourceAffinity source_affinity)
    : exec(exec),
      aliases(aliases),
      short_help(short_help),
      help(help),
      command_group(command_group),
      source_affinity(source_affinity),
      complete(complete) {}
VerbRecord::VerbRecord(CommandExecutorWithCallback exec_cb,
                       CommandCompleter complete,
                       std::initializer_list<std::string> aliases,
                       const char* short_help, const char* help,
                       CommandGroup command_group,
                       SourceAffinity source_affinity)
    : exec_cb(exec_cb),
      aliases(aliases),
      short_help(short_help),
      help(help),
      command_group(command_group),
      source_affinity(source_affinity),
      complete(complete) {}
VerbRecord::~VerbRecord() = default;

const std::map<Verb, VerbRecord>& GetVerbs() {
  static std::map<Verb, VerbRecord> all_verbs;
  if (all_verbs.empty()) {
    AppendBreakpointVerbs(&all_verbs);
    AppendControlVerbs(&all_verbs);
    AppendMemoryVerbs(&all_verbs);
    AppendProcessVerbs(&all_verbs);
    AppendSettingsVerbs(&all_verbs);
    AppendSharedVerbs(&all_verbs);
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

std::string VerbToString(Verb v) {
  const auto& verbs = GetVerbs();
  auto found = verbs.find(v);
  if (found == verbs.end())
    return std::string();
  return found->second.aliases[0];
}

}  // namespace zxdb
