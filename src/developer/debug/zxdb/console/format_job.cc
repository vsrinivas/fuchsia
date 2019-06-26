// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_job.h"

#include "src/developer/debug/zxdb/client/job.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

std::string GetJobContextName(const JobContext* job_context) {
  // When running, use the object name if any.
  std::string name;
  if (job_context->GetState() == JobContext::State::kAttached)
    name = job_context->GetJob()->GetName();

  return name;
}

}  // namespace

OutputBuffer FormatJobContext(ConsoleContext* context, const JobContext* job_context) {
  int id = context->IdForJobContext(job_context);
  const char* state = JobContextStateToString(job_context->GetState());

  // Koid string. This includes a trailing space when present so it can be
  // concat'd even when not present and things look nice.
  std::string koid_str;
  if (job_context->GetState() == JobContext::State::kAttached) {
    koid_str = fxl::StringPrintf("koid=%" PRIu64 " ", job_context->GetJob()->GetKoid());
  }

  std::string result = fxl::StringPrintf("Job %d [%s] %s", id, state, koid_str.c_str());
  result += GetJobContextName(job_context);
  return result;
}

OutputBuffer FormatJobList(ConsoleContext* context, int indent) {
  auto job_contexts = context->session()->system().GetJobContexts();

  int active_job_context_id = context->GetActiveJobContextId();

  // Sort by ID.
  std::vector<std::pair<int, JobContext*>> id_job_contexts;
  for (auto& job_context : job_contexts)
    id_job_contexts.push_back(std::make_pair(context->IdForJobContext(job_context), job_context));
  std::sort(id_job_contexts.begin(), id_job_contexts.end());

  std::string indent_str(indent, ' ');

  std::vector<std::vector<std::string>> rows;
  for (const auto& pair : id_job_contexts) {
    rows.emplace_back();
    std::vector<std::string>& row = rows.back();

    // "Current process" marker (or nothing). This pads by the indent to push
    // everything over.
    if (pair.first == active_job_context_id)
      row.emplace_back(indent_str + GetRightArrow());
    else
      row.push_back(indent_str);

    // ID.
    row.push_back(fxl::StringPrintf("%d", pair.first));

    // State and koid (if running).
    row.push_back(JobContextStateToString(pair.second->GetState()));
    if (pair.second->GetState() == JobContext::State::kAttached) {
      row.push_back(fxl::StringPrintf("%" PRIu64, pair.second->GetJob()->GetKoid()));
    } else {
      row.emplace_back();
    }

    row.push_back(GetJobContextName(pair.second));
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#", 0, Syntax::kSpecial),
               ColSpec(Align::kLeft, 0, "State"), ColSpec(Align::kRight, 0, "Koid"),
               ColSpec(Align::kLeft, 0, "Name")},
              rows, &out);
  return out;
}

const char* JobContextStateToString(JobContext::State state) {
  switch (state) {
    case JobContext::State::kNone:
      return "Not attached";
    case JobContext::State::kAttaching:
      return "Attaching";
    case JobContext::State::kAttached:
      return "Attached";
  }
  FXL_NOTREACHED();
  return "";
}

}  // namespace zxdb
