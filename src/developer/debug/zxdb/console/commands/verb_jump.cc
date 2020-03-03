// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_jump.h"

#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kJumpShortHelp[] = "jump / jmp: Set the instruction pointer to a different address.";
const char kJumpHelp[] =
    R"(jump <location>

  Alias: "jmp"

  Sets the instruction pointer of the thread to the given address. It does not
  continue execution. You can "step" or "continue" from the new location.

  You are responsible for what this means semantically since one can't
  generally change the instruction flow and expect things to work.

Location arguments

)" LOCATION_ARG_HELP("jump");

Err RunVerbJump(ConsoleContext* context, const Command& cmd) {
  if (Err err = AssertStoppedThreadCommand(context, cmd, true, "jump"); err.has_error())
    return err;

  if (cmd.args().size() != 1)
    return Err("The 'jump' command requires one argument for the location.");

  Location location;
  if (Err err = ResolveUniqueInputLocation(cmd.frame(), cmd.args()[0], true, &location);
      err.has_error())
    return err;

  cmd.thread()->JumpTo(location.address(), [thread = cmd.thread()->GetWeakPtr()](const Err& err) {
    Console* console = Console::get();
    if (err.has_error()) {
      console->Output(err);
    } else if (thread) {
      // Reset the current stack frame to the top to reflect the location the user has just jumped
      // to.
      console->context().SetActiveFrameIdForThread(thread.get(), 0);

      // Tell the user where they are.
      console->context().OutputThreadContext(thread.get(), StopInfo());
    }
  });

  return Err();
}

}  // namespace

VerbRecord GetJumpVerbRecord() {
  return VerbRecord(&RunVerbJump, &CompleteInputLocation, {"jump", "jmp"}, kJumpShortHelp,
                    kJumpHelp, CommandGroup::kStep);
}

}  // namespace zxdb
