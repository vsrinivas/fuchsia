// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_target.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Returns the process name of the given target, depending on the running
// process or the current app name, as applicable.
std::string GetTargetName(const Target* target) {
  // When running, use the object name if any.
  std::string name;
  if (target->GetState() == Target::State::kRunning)
    name = target->GetProcess()->GetName();

  // Otherwise fall back to the program name which is the first arg.
  if (name.empty()) {
    const std::vector<std::string>& args = target->GetArgs();
    if (!args.empty())
      name += args[0];
  }
  return name;
}

}  // namespace

OutputBuffer FormatTarget(ConsoleContext* context, const Target* target) {
  OutputBuffer out("Process ");
  out.Append(Syntax::kSpecial, std::to_string(context->IdForTarget(target)));

  out.Append(Syntax::kVariable, " state");
  out.Append(std::string("=") + FormatConsoleString(TargetStateToString(target->GetState())) + " ");

  if (target->GetState() == Target::State::kRunning) {
    out.Append(Syntax::kVariable, "koid");
    out.Append("=" + std::to_string(target->GetProcess()->GetKoid()) + " ");
  }

  out.Append(Syntax::kVariable, "name");
  out.Append(std::string("=") + FormatConsoleString(GetTargetName(target)));

  return out;
}

OutputBuffer FormatTargetList(ConsoleContext* context, int indent) {
  auto targets = context->session()->system().GetTargets();

  int active_target_id = context->GetActiveTargetId();

  // Sort by ID.
  std::vector<std::pair<int, Target*>> id_targets;
  for (auto& target : targets)
    id_targets.push_back(std::make_pair(context->IdForTarget(target), target));
  std::sort(id_targets.begin(), id_targets.end());

  std::string indent_str(indent, ' ');

  std::vector<std::vector<std::string>> rows;
  for (const auto& pair : id_targets) {
    rows.emplace_back();
    std::vector<std::string>& row = rows.back();

    // "Current process" marker (or nothing).
    if (pair.first == active_target_id)
      row.push_back(indent_str + GetCurrentRowMarker());
    else
      row.push_back(indent_str);

    // ID.
    row.push_back(std::to_string(pair.first));

    // State and koid (if running).
    row.push_back(TargetStateToString(pair.second->GetState()));
    if (pair.second->GetState() == Target::State::kRunning) {
      row.push_back(std::to_string(pair.second->GetProcess()->GetKoid()));
    } else {
      row.emplace_back();
    }

    row.push_back(GetTargetName(pair.second));
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#", 0, Syntax::kSpecial),
               ColSpec(Align::kLeft, 0, "State"), ColSpec(Align::kRight, 0, "Koid"),
               ColSpec(Align::kLeft, 0, "Name")},
              rows, &out);
  return out;
}

const char* TargetStateToString(Target::State state) {
  switch (state) {
    case Target::State::kNone:
      return "Not running";
    case Target::State::kStarting:
      return "Starting";
    case Target::State::kAttaching:
      return "Attaching";
    case Target::State::kRunning:
      return "Running";
  }
  FXL_NOTREACHED();
  return "";
}

}  // namespace zxdb
