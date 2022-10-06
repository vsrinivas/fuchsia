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

struct SequenceInfo {
  fxl::WeakPtr<Console> weak_console;
  std::vector<std::string> commands;
  size_t next_index = 0;
  fit::callback<void(Err)> cb;
};

fit::callback<void(const Err&)> MakeSequencedCommandCallback(SequenceInfo info) {
  return [info = std::move(info)](const Err& err) mutable {
    if (err.has_error())
      return info.cb(err);

    if (info.next_index >= info.commands.size())
      return info.cb(Err());  // Done executing.

    debug::MessageLoop::Current()->PostTask(FROM_HERE, [info = std::move(info)]() mutable {
      if (!info.weak_console)
        return info.cb(Err("Console gone."));

      std::string cur_line = info.commands[info.next_index];
      info.next_index++;
      info.weak_console->ProcessInputLine(cur_line, MakeSequencedCommandCallback(std::move(info)),
                                          false);
    });
  };
}

}  // namespace

void RunCommandSequence(Console* console, std::vector<std::string> commands,
                        fit::callback<void(Err)> cb) {
  if (commands.empty())
    return cb(Err());

  auto weak_console = console->GetWeakPtr();
  std::string cur_line = commands[0];
  console->ProcessInputLine(
      cur_line,
      MakeSequencedCommandCallback(SequenceInfo{.weak_console = console->GetWeakPtr(),
                                                .commands = std::move(commands),
                                                .next_index = 1,
                                                .cb = std::move(cb)}),
      false);
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
