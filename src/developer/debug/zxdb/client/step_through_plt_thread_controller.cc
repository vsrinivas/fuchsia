// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/step_through_plt_thread_controller.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/function_step.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

void StepThroughPltThreadController::InitWithThread(Thread* thread,
                                                    fit::callback<void(const Err&)> cb) {
  SetThread(thread);

  const Stack& stack = thread->GetStack();
  if (stack.empty())
    return cb(Err("Can't step, no frames."));
  const Frame* top_frame = stack[0];

  // Extract the ELF PLT symbol for the current location (the thread should be stopped at a PLT
  // symbol when InitWithThread() is called).
  const Location& cur_loc = top_frame->GetLocation();
  if (!cur_loc.symbol()) {
    FX_NOTREACHED();
    return cb(Err("Expecting a PLT symbol to step through."));
  }
  const ElfSymbol* elf_sym = cur_loc.symbol().Get()->As<ElfSymbol>();
  if (!elf_sym || elf_sym->elf_type() != ElfSymbolType::kPlt) {
    FX_NOTREACHED();
    return cb(Err("Expecting a PLT symbol to step through."));
  }

  const std::string linkage_name = elf_sym->linkage_name();
  plt_address_ = cur_loc.address();

  // The PLT trampoline will have the same name as the destinaion symbols, they'll all be called,
  // for example, "open" and they'll all be a PLT type (so "$plt(open)" in zxdb naming).
  // Currently ELF symbol lookup only takes mangled names, so we need to construct an identifier
  // based on the linkage name.
  Identifier plt_name(IdentifierComponent(SpecialIdentifier::kPlt, linkage_name));
  FX_DCHECK(plt_name.components().size() == 1);  // Expect one component for all ELF symbols.

  // Get the elf symbol name because we don't want to just match PLT entries. Querying for
  // $elf(open) will also match $plt(open) because PLT symbols are a subset of ELF symbols. These
  // extra matches should be harmless: we'll filter out our current PLT symbols and other modules'
  // PLT entries for the same symbol just won't be hit.
  Identifier elf_name(IdentifierComponent(SpecialIdentifier::kElf, linkage_name));

  // We expect the function name to resolve to two locations: the current one (the calling PLT
  // entry) and the destination one. There might be additional ones if there are duplicate symbols
  // (yikes) or other modules importing the same function (normal) but if there is only one it's our
  // calling location and the destination is unresolved.
  //
  // We could pass the function name directly to the "Until" controller but it will also match
  // our current location and will hit when we try to continue.
  //
  // There is some extra logic in the breakpoint that the "until" controller makes about dynamically
  // loaded libraries (like if this PLT thunk actually causes a module to be loaded) that we may
  // want in the future. If that's the case, we may want to just pass the function name to the
  // "until" controller and reach into its breakpoint and disable the current location.
  auto found = thread->GetProcess()->GetSymbols()->ResolveInputLocation(InputLocation(elf_name));

  // Filter out the current IP.
  found.erase(
      std::remove_if(found.begin(), found.end(),
                     [ip = plt_address_](const Location& cur) { return cur.address() == ip; }),
      found.end());

  Log("Got %zu matches for ELF symbol %s, running 'until' there.", found.size(),
      plt_name.components()[0].name().c_str());

  // When no matches were found, the destination can never be hit. Using the "until" controller at
  // this point will be like continuing the program which will lose the current location. In this
  // case, give up and stop the program so the user can figure out what they want to do.
  if (found.empty()) {
    cb(Err("Could not find destination of PLT trampoline."));
    return;
  }

  // Make the "until" controller the the resulting address(s).
  std::vector<InputLocation> input_locations;
  for (const auto& loc : found)
    input_locations.push_back(InputLocation(loc.address()));
  until_ = std::make_unique<UntilThreadController>(std::move(input_locations));
  until_->InitWithThread(thread, std::move(cb));
}

ThreadController::ContinueOp StepThroughPltThreadController::GetContinueOp() {
  return until_->GetContinueOp();
}

ThreadController::StopOp StepThroughPltThreadController::OnThreadStop(
    debug_ipc::ExceptionType stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  if (!until_) {
    Log("No destination for PLT step, stopping execution.");
    return ThreadController::StopOp::kStopDone;
  }

  Log("Checking if PLT stepping is complete.");
  auto result = until_->OnThreadStop(stop_type, hit_breakpoints);
  if (result == ThreadController::StopOp::kStopDone) {
    Log("PLT stepping complete.");
  } else {
    Log("Until controller reports it's not done yet.");
  }
  return result;
}

}  // namespace zxdb
