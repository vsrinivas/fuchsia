// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/nouns.h"

#include <algorithm>
#include <utility>

#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/console_context.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

void ListThreads(ConsoleContext* context, const Command& cmd) {
  // TODO(brettw) print threads. We should probably do a query on the system
  // so the most up-to-date thread name can be retrieved.
}

// Returns true if the thread noun was specified and therefore handled.
bool HandleThread(ConsoleContext* context, const Command& cmd) {
  if (!cmd.HasNoun(Noun::kThread))
    return false;

  if (cmd.GetNounIndex(Noun::kThread) == Command::kNoIndex) {
    // Just "thread" or "process 2 thread" specified, this lists available
    // threads.
    ListThreads(context, cmd);
    return true;
  }

  // Explicit index provided, this switches the current context. The thread
  // should be already resolved to a valid pointer if it was specified on the
  // command line (otherwise the command would have been rejected before here).
  FXL_DCHECK(cmd.thread());
  context->SetActiveThreadInTarget(cmd.thread());
  // Setting the active thread also sets the active target.
  context->SetActiveTarget(cmd.target());
  return true;
}

void ListProcesses(ConsoleContext* context, const Command& cmd) {
  auto targets = context->session()->system().GetAllTargets();

  // Sort by ID.
  std::vector<std::pair<int, Target*>> id_targets;
  for (auto& target : targets)
    id_targets.push_back(std::make_pair(context->IdForTarget(target), target));
  std::sort(id_targets.begin(), id_targets.end());

  OutputBuffer out;
  for (const auto& pair : id_targets) {
    out.Append(fxl::StringPrintf("%3d", pair.first));

    // Follow index by args (program name and params).
    const auto& args = pair.second->GetArgs();
    if (args.empty()) {
      out.Append(" <no name>");
    } else {
      for (const auto& arg : pair.second->GetArgs()) {
        out.Append(" ");
        out.Append(arg);
      }
    }
    out.Append("\n");
  }

  Console::get()->Output(std::move(out));
}

// Returns true if the process noun was specified and therefore handled.
bool HandleProcess(ConsoleContext* context, const Command& cmd) {
  if (!cmd.HasNoun(Noun::kProcess))
    return false;

  if (cmd.GetNounIndex(Noun::kProcess) == Command::kNoIndex) {
    // Just "process", this lists available processes.
    ListProcesses(context, cmd);
    return true;
  }

  // Explicit index provided, this switches the current context. The target
  // should be already resolved to a valid pointer if it was specified on the
  // command line (otherwise the command would have been rejected before here).
  context->SetActiveTarget(cmd.target());
  return true;
}

}  // namespace

Err ExecuteNoun(ConsoleContext* context, const Command& cmd) {
  Err result;
  // Work backwards in specificity (frame -> thread -> process).

  // TODO(brettw) frame.
  if (HandleThread(context, cmd))
    return result;
  if (HandleProcess(context, cmd))
    return result;
  return result;
}

}  // namespace zxdb
