// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_quit_agent.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"

namespace zxdb {

namespace {

const char kQuitAgentShortHelp[] = R"(quit-agent: Quits the debug agent.)";
const char kQuitAgentHelp[] =
    R"(quit-agent

  Quits the connected debug agent running on the target.)";

Err RunVerbQuitAgent(ConsoleContext* context, const Command& cmd) {
  context->session()->QuitAgent([](const Err& err) {
    if (err.has_error()) {
      Console::get()->Output(err);
    } else {
      Console::get()->Output("Successfully stopped the debug agent.");
    }
  });

  return Err();
}

}  // namespace

VerbRecord GetQuitAgentVerbRecord() {
  return VerbRecord(&RunVerbQuitAgent, {"quit-agent"}, kQuitAgentShortHelp, kQuitAgentHelp,
                    CommandGroup::kGeneral);
}

}  // namespace zxdb
