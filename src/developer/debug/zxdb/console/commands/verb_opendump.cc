// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_opendump.h"

#include <filesystem>

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/nouns.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kOpenDumpShortHelp[] = R"(opendump: Open a dump file for debugging.)";
const char kOpenDumpHelp[] =
    R"(opendump <path>

  Opens a dump file. Currently only the 'minidump' format is supported.

  With the dump open, you will be able to list processes and threads, view the
  memory map at the time the dump occurred, obtain a backtrace of threads, and
  read some memory from the time of the crash. What memory is readable depends
  on what the dump chose to include and what binaries are available from the
  original system.
)";

void DoCompleteOpenDump(const Command& cmd, const std::string& prefix,
                        std::vector<std::string>* completions) {
  if (!cmd.args().empty())
    return;

  std::error_code ec;

  std::filesystem::path path;
  std::string filename;

  if (prefix.empty()) {
    path = std::filesystem::current_path(ec);
    if (ec)
      return;
  } else if (std::filesystem::exists(prefix, ec)) {
    if (!std::filesystem::is_directory(prefix, ec)) {
      completions->push_back(prefix);
      return;
    }

    path = std::filesystem::path(prefix) / "";
  } else {
    auto path_parts = std::filesystem::path(prefix);
    filename = path_parts.filename();

    if (filename.empty())
      return;
    path = path_parts.parent_path();

    if (path.empty()) {
      path = std::filesystem::current_path(ec);
      if (ec)
        return;
    } else if (!std::filesystem::is_directory(path, ec)) {
      return;
    }
  }

  for (const auto& item : std::filesystem::directory_iterator(path, ec)) {
    auto found = std::string(item.path().filename());

    if (!StringStartsWith(found, filename))
      continue;

    auto completion = prefix + found.substr(filename.size());
    if (item.is_directory(ec))
      completion += "/";
    completions->push_back(completion);
  }
}

void RunVerbOpendump(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  std::string path;

  if (cmd.args().size() == 0) {
    return cmd_context->ReportError(Err(ErrType::kInput, "Need path to open."));
  } else if (cmd.args().size() == 1) {
    path = cmd.args()[0];
  } else {
    return cmd_context->ReportError(Err(ErrType::kInput, "Too many arguments."));
  }

  cmd_context->GetConsoleContext()->session()->OpenMinidump(path, [cmd_context](const Err& err) {
    if (err.has_error())
      return cmd_context->ReportError(err);
    cmd_context->Output("Dump loaded successfully.\n");
  });
  cmd_context->Output("Opening dump file...\n");
}

}  // namespace

VerbRecord GetOpendumpVerbRecord() {
  return VerbRecord(&RunVerbOpendump, &DoCompleteOpenDump, {"opendump"}, kOpenDumpShortHelp,
                    kOpenDumpHelp, CommandGroup::kGeneral, SourceAffinity::kNone);
}

}  // namespace zxdb
