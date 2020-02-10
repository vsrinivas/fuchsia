// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_sym_near.h"

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

namespace {

const char kSymNearShortHelp[] = "sym-near / sn: Print symbol for an address.";
const char kSymNearHelp[] =
    R"(sym-near <address-expression>

  Alias: "sn"

  Finds the symbol nearest to the given address. This command is useful for
  finding what a pointer or a code location refers to.

  The address can be an explicit number or any expression ("help print") that
  evaluates to a memory address.

Example

  sym-near 0x12345670
  process 2 sym-near &x
)";

Err RunVerbSymNear(ConsoleContext* context, const Command& cmd) {
  if (Err err = cmd.ValidateNouns({Noun::kProcess}); err.has_error())
    return err;
  if (Err err = AssertRunningTarget(context, "sym-near", cmd.target()); err.has_error())
    return err;

  return EvalCommandAddressExpression(
      cmd, "sym-near", GetEvalContextForCommand(cmd),
      [weak_process = cmd.target()->GetProcess()->GetWeakPtr()](const Err& err, uint64_t address,
                                                                std::optional<uint64_t> size) {
        Console* console = Console::get();
        if (err.has_error()) {
          console->Output(err);  // Evaluation error.
          return;
        }
        if (!weak_process) {
          // Process has been destroyed during evaluation. Normally a message will be printed when
          // that happens so we can skip reporting the error.
          return;
        }

        auto locations = weak_process->GetSymbols()->ResolveInputLocation(InputLocation(address));
        FXL_DCHECK(locations.size() == 1u);

        FormatLocationOptions opts(weak_process->GetTarget());
        opts.always_show_addresses = true;
        opts.show_params = true;
        opts.show_file_line = true;

        console->Output(FormatLocation(locations[0], opts));
      });
}

}  // namespace

VerbRecord GetSymNearVerbRecord() {
  VerbRecord sym_near(&RunVerbSymNear, {"sym-near", "sn"}, kSymNearShortHelp, kSymNearHelp,
                      CommandGroup::kSymbol);
  sym_near.param_type = VerbRecord::kOneParam;
  return sym_near;
}

}  // namespace zxdb
