// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_target.h"

#include <optional>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

std::string GetComponentName(const std::optional<debug_ipc::ComponentInfo>& component_info) {
  if (!component_info)
    return "";
  return component_info->url.substr(component_info->url.find_last_of('/') + 1);
}

}  // namespace

OutputBuffer FormatTarget(ConsoleContext* context, const Target* target) {
  OutputBuffer out("Process ");
  out.Append(Syntax::kSpecial, std::to_string(context->IdForTarget(target)));

  out.Append(Syntax::kVariable, " state");
  out.Append("=" + FormatConsoleString(TargetStateToString(target->GetState())));

  if (target->GetState() == Target::State::kRunning) {
    out.Append(Syntax::kVariable, " koid");
    out.Append("=" + std::to_string(target->GetProcess()->GetKoid()));
  }

  if (auto process = target->GetProcess()) {
    out.Append(Syntax::kVariable, " name");
    out.Append("=" + FormatConsoleString(process->GetName()));
    if (auto component = process->GetComponentInfo()) {
      // The component info can be long, put on separate lines.
      out.Append("\n");
      out.Append(Syntax::kVariable, "  moniker");
      out.Append("=" + FormatConsoleString(component->moniker));
      out.Append("\n");
      out.Append(Syntax::kVariable, "  url");
      out.Append("=" + FormatConsoleString(component->url));
    }
  }
  out.Append("\n");

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
  for (const auto& [id, target] : id_targets) {
    rows.emplace_back();
    std::vector<std::string>& row = rows.back();

    // "Current process" marker (or nothing).
    if (id == active_target_id)
      row.push_back(indent_str + GetCurrentRowMarker());
    else
      row.push_back(indent_str);

    // ID.
    row.push_back(std::to_string(id));

    // State and koid (if running).
    row.push_back(TargetStateToString(target->GetState()));
    if (auto process = target->GetProcess()) {
      row.push_back(std::to_string(process->GetKoid()));
      row.push_back(process->GetName());
      row.push_back(GetComponentName(process->GetComponentInfo()));
    } else {
      row.emplace_back();
    }
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#", 0, Syntax::kSpecial),
               ColSpec(Align::kLeft, 0, "State"), ColSpec(Align::kRight, 0, "Koid"),
               ColSpec(Align::kLeft, 0, "Name"), ColSpec(Align::kLeft, 0, "Component")},
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
  FX_NOTREACHED();
  return "";
}

}  // namespace zxdb
