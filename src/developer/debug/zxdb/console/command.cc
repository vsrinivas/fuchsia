// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/nouns.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

const int Command::kNoIndex;

Command::Command() = default;
Command::~Command() = default;

bool Command::HasNoun(Noun noun) const { return nouns_.find(noun) != nouns_.end(); }

int Command::GetNounIndex(Noun noun) const {
  auto found = nouns_.find(noun);
  if (found == nouns_.end())
    return kNoIndex;
  return found->second;
}

void Command::SetNoun(Noun noun, int index) {
  FX_DCHECK(nouns_.find(noun) == nouns_.end());
  nouns_[noun] = index;
}

Err Command::ValidateNouns(std::initializer_list<Noun> allowed_nouns) const {
  for (const auto& pair : nouns_) {
    if (std::find(allowed_nouns.begin(), allowed_nouns.end(), pair.first) == allowed_nouns.end()) {
      return Err(ErrType::kInput, fxl::StringPrintf("\"%s\" may not be specified for this command.",
                                                    NounToString(pair.first).c_str()));
    }
  }
  return Err();
}

bool Command::HasSwitch(int id) const { return switches_.find(id) != switches_.end(); }

std::string Command::GetSwitchValue(int id) const {
  auto found = switches_.find(id);
  if (found == switches_.end())
    return std::string();
  return found->second;
}

void Command::SetSwitch(int id, std::string str) { switches_[id] = std::move(str); }

void DispatchCommand(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (cmd.verb() == Verb::kNone) {
    ExecuteNoun(cmd, cmd_context);
    return;
  }

  const auto& verbs = GetVerbs();
  const auto& found = verbs.find(cmd.verb());
  if (found == verbs.end()) {
    cmd_context->ReportError(
        Err(ErrType::kInput, "Invalid verb \"" + VerbToString(cmd.verb()) + "\"."));
    return;
  }

  found->second.exec(cmd, cmd_context);
}

}  // namespace zxdb
