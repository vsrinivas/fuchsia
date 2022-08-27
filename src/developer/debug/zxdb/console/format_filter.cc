// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_filter.h"

#include <string>

#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/string_util.h"

namespace zxdb {

OutputBuffer FormatFilter(const ConsoleContext* context, const Filter* filter) {
  OutputBuffer out("Filter ");
  out.Append(Syntax::kSpecial, std::to_string(context->IdForFilter(filter)));

  out.Append(Syntax::kVariable, " type");
  out.Append("=" + FormatConsoleString(debug_ipc::Filter::TypeToString(filter->type())) + " ");

  if (!filter->pattern().empty()) {
    out.Append(Syntax::kVariable, "pattern");
    out.Append("=" + FormatConsoleString(filter->pattern()) + " ");
  }

  if (filter->job_koid()) {
    out.Append(Syntax::kVariable, "job");
    out.Append("=" + std::to_string(filter->job_koid()) + " ");
  }

  if (!filter->is_valid()) {
    out.Append(Syntax::kComment, "(invalid) ");
  }

  return out;
}

OutputBuffer FormatFilterList(ConsoleContext* context, int indent) {
  std::string indent_str(indent, ' ');

  int active_filter_id = context->GetActiveFilterId();
  auto filters = context->session()->system().GetFilters();

  std::vector<std::vector<std::string>> rows;
  for (auto& filter : filters) {
    auto id = context->IdForFilter(filter);

    std::vector<std::string>& row = rows.emplace_back();

    // "Current thread" marker and left padding.
    if (id == active_filter_id) {
      row.push_back(indent_str + GetCurrentRowMarker());
    } else {
      row.emplace_back(indent_str);
    }

    row.push_back(std::to_string(id));
    row.push_back(debug_ipc::Filter::TypeToString(filter->type()));
    row.push_back(filter->pattern());
    row.push_back(filter->job_koid() ? std::to_string(filter->job_koid()) : "");
    row.push_back(filter->is_valid() ? "" : "(invalid)");
  }

  OutputBuffer out;
  if (rows.empty()) {
    out.Append(indent_str + "No filters.\n");
  } else {
    FormatTable(
        {ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#", 0, Syntax::kSpecial),
         ColSpec(Align::kLeft, 0, "Type"), ColSpec(Align::kLeft, 0, "Pattern"),
         ColSpec(Align::kRight, 0, "Job"), ColSpec(Align::kRight, 0, "", 0, Syntax::kComment)},
        rows, &out);
  }

  return out;
}

}  // namespace zxdb
