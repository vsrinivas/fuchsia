// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_filter.h"

#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/job.h"
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

  out.Append(Syntax::kVariable, " pattern");
  out.Append("=" + FormatConsoleString(filter->pattern()) + " ");

  if (filter->pattern().empty()) {
    out.Append(Syntax::kComment, "(disabled) ");
  } else if (filter->pattern() == Filter::kAllProcessesPattern) {
    out.Append(Syntax::kComment, "(all processes) ");
  }

  out.Append(Syntax::kVariable, "job");
  out.Append("=");
  if (filter->job()) {
    out.Append(std::to_string(context->IdForJob(filter->job())));
  } else {
    out.Append("*");
    out.Append(Syntax::kComment, " (all attached jobs)");
  }

  return out;
}

OutputBuffer FormatFilterList(ConsoleContext* context, const Job* for_job, int indent) {
  std::string indent_str(indent, ' ');

  int active_filter_id = context->GetActiveFilterId();
  auto filters = context->session()->system().GetFilters();

  std::vector<std::vector<std::string>> rows;
  for (auto& filter : filters) {
    if (for_job && filter->job() && filter->job() != for_job)
      continue;

    auto id = context->IdForFilter(filter);

    std::vector<std::string>& row = rows.emplace_back();

    // "Current thread" marker and left padding.
    if (id == active_filter_id) {
      row.push_back(indent_str + GetCurrentRowMarker());
    } else {
      row.emplace_back(indent_str);
    }

    row.push_back(std::to_string(id));
    row.push_back(filter->pattern());

    if (filter->job()) {
      auto job_id = context->IdForJob(filter->job());
      row.push_back(std::to_string(job_id));
    } else {
      row.push_back("*");
    }
  }

  OutputBuffer out;
  if (rows.empty()) {
    if (for_job) {
      out.Append(indent_str + "No filters for job ");
      out.Append(Syntax::kSpecial, std::to_string(context->IdForJob(for_job)));
      out.Append(".\n");
    } else {
      out.Append(indent_str + "No filters.\n");
    }
  } else {
    if (for_job) {
      out.Append(indent_str + "Filters for job ");
      out.Append(Syntax::kSpecial, std::to_string(context->IdForJob(for_job)));
      out.Append(" only:\n");
    }
    FormatTable({ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#", 0, Syntax::kSpecial),
                 ColSpec(Align::kLeft, 0, "pattern"), ColSpec(Align::kRight, 0, "job")},
                rows, &out);
  }

  return out;
}

}  // namespace zxdb
