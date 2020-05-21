// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_job.h"

#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

std::string GetJobName(const Job* job) {
  // When running, use the object name if any.
  std::string name;
  if (job->state() == Job::State::kAttached)
    name = job->name();

  return name;
}

}  // namespace

OutputBuffer FormatJob(ConsoleContext* context, const Job* job) {
  OutputBuffer out("Job ");
  out.Append(Syntax::kSpecial, std::to_string(context->IdForJob(job)));

  out.Append(Syntax::kVariable, " state");
  out.Append(std::string("=") + FormatConsoleString(JobStateToString(job->state())) + " ");

  if (job->state() == Job::State::kAttached) {
    out.Append(Syntax::kVariable, "koid");
    out.Append("=" + std::to_string(job->koid()) + " ");
  }

  out.Append(Syntax::kVariable, "name");
  out.Append(std::string("=") + FormatConsoleString(GetJobName(job)));

  return out;
}

OutputBuffer FormatJobList(ConsoleContext* context, int indent) {
  auto jobs = context->session()->system().GetJobs();

  int active_job_id = context->GetActiveJobId();

  // Sort by ID.
  std::vector<std::pair<int, Job*>> id_jobs;
  for (auto& job : jobs)
    id_jobs.push_back(std::make_pair(context->IdForJob(job), job));
  std::sort(id_jobs.begin(), id_jobs.end());

  std::string indent_str(indent, ' ');

  std::vector<std::vector<std::string>> rows;
  for (const auto& pair : id_jobs) {
    rows.emplace_back();
    std::vector<std::string>& row = rows.back();

    // "Current process" marker (or nothing). This pads by the indent to push
    // everything over.
    if (pair.first == active_job_id)
      row.emplace_back(indent_str + GetCurrentRowMarker());
    else
      row.push_back(indent_str);

    // ID.
    row.push_back(std::to_string(pair.first));

    // State and koid (if running).
    row.push_back(JobStateToString(pair.second->state()));
    if (pair.second->state() == Job::State::kAttached) {
      row.push_back(fxl::StringPrintf("%" PRIu64, pair.second->koid()));
    } else {
      row.emplace_back();
    }

    row.push_back(GetJobName(pair.second));
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#", 0, Syntax::kSpecial),
               ColSpec(Align::kLeft, 0, "State"), ColSpec(Align::kRight, 0, "Koid"),
               ColSpec(Align::kLeft, 0, "Name")},
              rows, &out);
  return out;
}

const char* JobStateToString(Job::State state) {
  switch (state) {
    case Job::State::kNone:
      return "Not attached";
    case Job::State::kAttaching:
      return "Attaching";
    case Job::State::kAttached:
      return "Attached";
  }
  FX_NOTREACHED();
  return "";
}

}  // namespace zxdb
