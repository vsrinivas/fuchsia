// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_savedump.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kSaveDumpShortHelp[] = "savedump: Save a minidump file of the current process.";
const char kSaveDumpHelp[] =
    R"(savedump <file>

  Save a minidump file of the current process.

  <file> is the path to the saved file. The parent directory must already exist.
)";

// Commit |core_data| to the filesystem at |path|. |path| must already exist before calling this
// function.
//
// Returns true on successful completion of write operation, false on failure.
Err WriteCoreDataToFile(const std::filesystem::path& path, const std::vector<uint8_t>& core_data) {
  if (path.has_parent_path()) {
    if (!std::filesystem::exists(path.parent_path())) {
      return Err(ErrType::kInput, "Path does not exist: " + path.parent_path().string());
    }
  } else if (std::filesystem::exists(path)) {
    return Err(ErrType::kAlreadyExists, "File already exists: " + path.filename().string());
  }

  std::fstream f(path, std::ios::trunc | std::ios::out | std::ios::binary);
  f.write(reinterpret_cast<const char*>(core_data.data()), core_data.size());

  if (f.bad()) {
    return Err(ErrType::kGeneral, "Failed to write minidump data to file.");
  }

  f.close();

  return Err();
}

Err RunVerbSaveDump(ConsoleContext* context, const Command& cmd,
                    CommandCallback callback = nullptr) {
  if (cmd.args().empty()) {
    return Err(ErrType::kInput, "Please specify a file.");
  } else if (cmd.args().size() > 1) {
    return Err(ErrType::kInput, "Too many arguments.");
  }

  std::filesystem::path path = cmd.args()[0];

  if (std::filesystem::exists(path)) {
    return Err(ErrType::kAlreadyExists, "Path: " + path.string() +
                                            " already exists. Please choose a different file name "
                                            "or delete the existing file.");
  }

  if (Err e = AssertAllStoppedThreadsCommand(context, cmd, "savedump"); e.has_error()) {
    return e;
  }

  debug_ipc::SaveMinidumpRequest request = {};
  request.process_koid = cmd.target()->GetProcess()->GetKoid();

  context->session()->remote_api()->SaveMinidump(
      request, [path = path, callback = std::move(callback)](
                   const Err& err, debug_ipc::SaveMinidumpReply reply) mutable {
        Err e;
        if (err.ok()) {
          Err write_err = WriteCoreDataToFile(path, reply.core_data);
          if (write_err.ok()) {
            Console::get()->Output("Minidump written to " + path.string());
          } else {
            e = write_err;
          }
        } else {
          Console::get()->Output("Failed to collect minidump: " + err.msg());
        }

        if (callback) {
          callback(e);
        }
      });

  Console::get()->Output("Saving minidump...\n");

  return Err();
}
}  // namespace

VerbRecord GetSaveDumpVerbRecord() {
  return VerbRecord(&RunVerbSaveDump, {"savedump"}, kSaveDumpShortHelp, kSaveDumpHelp,
                    CommandGroup::kGeneral, SourceAffinity::kNone);
}

}  // namespace zxdb
