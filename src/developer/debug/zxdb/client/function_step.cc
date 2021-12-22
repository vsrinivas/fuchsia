// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/function_step.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"

namespace zxdb {

namespace {

// Plt entries are the stubs the linker inserts into a binary when there is a call to another
// shared library (including system calls in the VDSO). The stub might handle dynamically resolving
// the symbol and references the address that the dynamic loader sets for the destination of the
// function call.
//
// We normally want to tread these stubs as if they don't exist. If the user steps into such a call,
// we want to stop in the destination of the call.
//
// This function returns true if the location points to the first instruction of a PLT stub. This
// will not work for subsequent instructions (the ELF symbols that correspond to the PLT entries
// don't have a length).
bool IsPltStub(const Location& loc) {
  if (!loc.symbol())
    return false;  // Unsymbolized.

  const ElfSymbol* elf_sym = loc.symbol().Get()->As<ElfSymbol>();
  if (!elf_sym)
    return false;  // Not an ELF symbol.

  return elf_sym->elf_type() == ElfSymbolType::kPlt;
}

}  // namespace

const char* FunctionStepToString(FunctionStep fs) {
  switch (fs) {
    case FunctionStep::kDefault:
      return "kDefault";
    case FunctionStep::kStepThroughPlt:
      return "kStepThroughPlt";
    case FunctionStep::kStepNoLineInfo:
      return "kStepNoLineInfo";
    case FunctionStep::kStepOut:
      return "kStepOut";
  }
  return "<INVALID>";
}

// If anything goes wrong, this function returns kDefault to indicate nothing special should happen.
// The calling code would then either stop or continue as it would normally.
FunctionStep GetFunctionStepAction(Thread* thread) {
  const Stack& stack = thread->GetStack();
  if (stack.empty())
    return FunctionStep::kDefault;

  const Frame* frame = stack[0];
  const Location& loc = frame->GetLocation();

  // Always step through PLT stubs. The caller will evaluate whether the function should be stepped
  // into or over when the destination function is reached.
  if (IsPltStub(loc))
    return FunctionStep::kStepThroughPlt;

  if (!loc.symbol()) {
    // Unsymbolized code, check the user preference for what to do.
    System& system = thread->session()->system();
    if (system.settings().GetBool(ClientSettings::System::kSkipUnsymbolized))
      return FunctionStep::kStepOut;
    return FunctionStep::kDefault;
  }

  // TODO(fxbug.dev/5442) add functionality for determining whether this call is a system source
  // call. We probably want to skip over all calls to libc.so by default and return kStepOut. For
  // libc code that's inlined, we may want to be smarter, like for std::function we'd want to step
  // through until we get to user code for for std::vector maybe we'd want to step out.

  return FunctionStep::kDefault;
}

}  // namespace zxdb
