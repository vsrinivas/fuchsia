// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command.h"

#include <algorithm>

#include "garnet/bin/zxdb/console/nouns.h"
#include "garnet/bin/zxdb/console/verbs.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

const int Command::kNoIndex;

Command::Command() = default;
Command::~Command() = default;

bool Command::HasNoun(Noun noun) const {
  return nouns_.find(noun) != nouns_.end();
}

int Command::GetNounIndex(Noun noun) const {
  auto found = nouns_.find(noun);
  if (found == nouns_.end())
    return kNoIndex;
  return found->second;
}

void Command::SetNoun(Noun noun, int index) {
  FXL_DCHECK(nouns_.find(noun) == nouns_.end());
  nouns_[noun] = index;
}

Err Command::ValidateNouns(std::initializer_list<Noun> allowed_nouns) const {
  for (const auto& pair : nouns_) {
    if (std::find(allowed_nouns.begin(), allowed_nouns.end(), pair.first) ==
        allowed_nouns.end()) {
      return Err(
          ErrType::kInput,
          fxl::StringPrintf("\"%s\" may not be specified for this command.",
                            NounToString(pair.first).c_str()));
    }
  }
  return Err();
}

bool Command::HasSwitch(int id) const {
  return switches_.find(id) != switches_.end();
}

std::string Command::GetSwitchValue(int id) const {
  auto found = switches_.find(id);
  if (found == switches_.end())
    return std::string();
  return found->second;
}

void Command::SetSwitch(int id, std::string str) {
  switches_[id] = std::move(str);
}

Err DispatchCommand(ConsoleContext* context, const Command& cmd,
                    CommandCallback callback) {
  if (cmd.verb() == Verb::kNone)
    return ExecuteNoun(context, cmd);

  const auto& verbs = GetVerbs();
  const auto& found = verbs.find(cmd.verb());
  if (found == verbs.end()) {
    return Err(ErrType::kInput,
               "Invalid verb \"" + VerbToString(cmd.verb()) + "\".");
  }

  auto& verb_record = found->second;
  if (verb_record.exec_cb) {
    return verb_record.exec_cb(context, cmd, callback);
  } else {
    Err original_err = verb_record.exec(context, cmd);
    if (callback) {
      // We need to call the callback to let the caller know they ran a command
      // that doesn't receive callbacks.
      Err callback_err =
          original_err.has_error()
              ? original_err
              : Err("Command was processed but it doesn't receive "
                    "callbacks. Going to interactive mode.");
      // Commands without callbacks never quit by callback.
      callback(callback_err);
    }
    return original_err;
  }
}

}  // namespace zxdb
