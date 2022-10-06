// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_sequence.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/split_string.h"

namespace zxdb {

namespace {

// Returns a child CommandContext of the given one. Upon completion, this child will run the
// next command in the sequence.
fxl::RefPtr<CommandContext> MakeSequencedCommandContext(std::vector<std::string> commands,
                                                        size_t next_index,
                                                        fxl::RefPtr<CommandContext> cmd_context) {
  return fxl::MakeRefCounted<NestedCommandContext>(
      cmd_context,
      [commands = std::move(commands), next_index, cmd_context](const Err& err) mutable {
        if (err.has_error())
          return;  // Can't continue.

        if (next_index >= commands.size())
          return;  // Success.

        debug::MessageLoop::Current()->PostTask(
            FROM_HERE, [commands = std::move(commands), next_index, cmd_context]() {
              if (!cmd_context->console())
                return;

              std::string cur_line = commands[next_index];
              cmd_context->console()->ProcessInputLine(
                  cur_line,
                  MakeSequencedCommandContext(std::move(commands), next_index + 1,
                                              std::move(cmd_context)),
                  false);
            });
      });
}

}  // namespace

void RunCommandSequence(Console* console, std::vector<std::string> commands,
                        fxl::RefPtr<CommandContext> cmd_context) {
  if (commands.empty())
    return;

  auto weak_console = console->GetWeakPtr();
  std::string cur_line = commands[0];
  console->ProcessInputLine(
      cur_line, MakeSequencedCommandContext(std::move(commands), 1, std::move(cmd_context)), false);
}

ErrOr<std::vector<std::string>> ReadCommandsFromFile(const std::string& path) {
  std::string contents;
  if (!files::ReadFileToString(path, &contents))
    return Err("Could not read file \"" + path + "\"");

  std::vector<std::string> result;
  for (auto& cmd :
       fxl::SplitStringCopy(contents, "\n", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty)) {
    result.push_back(std::move(cmd));
  }

  return result;
}

}  // namespace zxdb
