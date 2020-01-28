// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/verbs.h"

#include "src/developer/debug/zxdb/console/commands/verb_aspace.h"
#include "src/developer/debug/zxdb/console/commands/verb_attach.h"
#include "src/developer/debug/zxdb/console/commands/verb_attach_job.h"
#include "src/developer/debug/zxdb/console/commands/verb_break.h"
#include "src/developer/debug/zxdb/console/commands/verb_clear.h"
#include "src/developer/debug/zxdb/console/commands/verb_cls.h"
#include "src/developer/debug/zxdb/console/commands/verb_connect.h"
#include "src/developer/debug/zxdb/console/commands/verb_detach.h"
#include "src/developer/debug/zxdb/console/commands/verb_disable.h"
#include "src/developer/debug/zxdb/console/commands/verb_disassemble.h"
#include "src/developer/debug/zxdb/console/commands/verb_disconnect.h"
#include "src/developer/debug/zxdb/console/commands/verb_enable.h"
#include "src/developer/debug/zxdb/console/commands/verb_help.h"
#include "src/developer/debug/zxdb/console/commands/verb_kill.h"
#include "src/developer/debug/zxdb/console/commands/verb_libs.h"
#include "src/developer/debug/zxdb/console/commands/verb_mem_analyze.h"
#include "src/developer/debug/zxdb/console/commands/verb_mem_read.h"
#include "src/developer/debug/zxdb/console/commands/verb_opendump.h"
#include "src/developer/debug/zxdb/console/commands/verb_ps.h"
#include "src/developer/debug/zxdb/console/commands/verb_quit.h"
#include "src/developer/debug/zxdb/console/commands/verb_quit_agent.h"
#include "src/developer/debug/zxdb/console/commands/verb_run.h"
#include "src/developer/debug/zxdb/console/commands/verb_stack.h"
#include "src/developer/debug/zxdb/console/commands/verb_status.h"
#include "src/developer/debug/zxdb/console/commands/verb_stderr.h"
#include "src/developer/debug/zxdb/console/commands/verb_stdout.h"
#include "src/developer/debug/zxdb/console/commands/verb_sys_info.h"
#include "src/developer/debug/zxdb/console/commands/verb_watch.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

VerbRecord::VerbRecord() = default;
VerbRecord::VerbRecord(CommandExecutor exec, std::initializer_list<std::string> aliases,
                       const char* short_help, const char* help, CommandGroup command_group,
                       SourceAffinity source_affinity)
    : exec(std::move(exec)),
      aliases(aliases),
      short_help(short_help),
      help(help),
      command_group(command_group),
      source_affinity(source_affinity) {}

VerbRecord::VerbRecord(CommandExecutorWithCallback exec_cb,
                       std::initializer_list<std::string> aliases, const char* short_help,
                       const char* help, CommandGroup command_group, SourceAffinity source_affinity)
    : exec_cb(std::move(exec_cb)),
      aliases(aliases),
      short_help(short_help),
      help(help),
      command_group(command_group),
      source_affinity(source_affinity) {}

VerbRecord::VerbRecord(CommandExecutor exec, CommandCompleter complete,
                       std::initializer_list<std::string> aliases, const char* short_help,
                       const char* help, CommandGroup command_group, SourceAffinity source_affinity)
    : exec(std::move(exec)),
      aliases(aliases),
      short_help(short_help),
      help(help),
      command_group(command_group),
      source_affinity(source_affinity),
      complete(std::move(complete)) {}

VerbRecord::VerbRecord(CommandExecutorWithCallback exec_cb, CommandCompleter complete,
                       std::initializer_list<std::string> aliases, const char* short_help,
                       const char* help, CommandGroup command_group, SourceAffinity source_affinity)
    : exec_cb(std::move(exec_cb)),
      aliases(aliases),
      short_help(short_help),
      help(help),
      command_group(command_group),
      source_affinity(source_affinity),
      complete(std::move(complete)) {}

VerbRecord::~VerbRecord() = default;

const std::map<Verb, VerbRecord>& GetVerbs() {
  static std::map<Verb, VerbRecord> all_verbs;
  if (all_verbs.empty()) {
    AppendSettingsVerbs(&all_verbs);
    AppendSharedVerbs(&all_verbs);
    AppendSymbolVerbs(&all_verbs);
    AppendThreadVerbs(&all_verbs);

    all_verbs[Verb::kAspace] = GetAspaceVerbRecord();
    all_verbs[Verb::kAttach] = GetAttachVerbRecord();
    all_verbs[Verb::kAttachJob] = GetAttachJobVerbRecord();
    all_verbs[Verb::kBreak] = GetBreakVerbRecord();
    all_verbs[Verb::kClear] = GetClearVerbRecord();
    all_verbs[Verb::kCls] = GetClsVerbRecord();
    all_verbs[Verb::kConnect] = GetConnectVerbRecord();
    all_verbs[Verb::kDetach] = GetDetachVerbRecord();
    all_verbs[Verb::kDisable] = GetDisableVerbRecord();
    all_verbs[Verb::kDisconnect] = GetDisconnectVerbRecord();
    all_verbs[Verb::kDisassemble] = GetDisassembleVerbRecord();
    all_verbs[Verb::kEnable] = GetEnableVerbRecord();
    all_verbs[Verb::kHelp] = GetHelpVerbRecord();
    all_verbs[Verb::kKill] = GetKillVerbRecord();
    all_verbs[Verb::kLibs] = GetLibsVerbRecord();
    all_verbs[Verb::kListProcesses] = GetPsVerbRecord();
    all_verbs[Verb::kMemAnalyze] = GetMemAnalyzeVerbRecord();
    all_verbs[Verb::kMemRead] = GetMemReadVerbRecord();
    all_verbs[Verb::kOpenDump] = GetOpendumpVerbRecord();
    all_verbs[Verb::kQuitAgent] = GetQuitAgentVerbRecord();
    all_verbs[Verb::kQuit] = GetQuitVerbRecord();
    all_verbs[Verb::kRun] = GetRunVerbRecord();
    all_verbs[Verb::kStatus] = GetStatusVerbRecord();
    all_verbs[Verb::kStdout] = GetStdoutVerbRecord();
    all_verbs[Verb::kStderr] = GetStderrVerbRecord();
    all_verbs[Verb::kSysInfo] = GetSysInfoVerbRecord();
    all_verbs[Verb::kStack] = GetStackVerbRecord();
    all_verbs[Verb::kWatch] = GetWatchVerbRecord();

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
