// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

namespace {

void PrintStub(Writer* writer, Syscall* syscall) {
  writer->Printf("func Sys_%s(", syscall->snake_name().c_str());
  for (size_t i = 0; i < syscall->num_kernel_args(); ++i) {
    if (i > 0) {
      writer->Puts(", ");
    }
    const StructMember& arg = syscall->kernel_arguments()[i];
    writer->Printf("%s %s", RemapReservedGoName(arg.name()).c_str(), GetGoName(arg.type()).c_str());
  }

  writer->Puts(")");
  if (!syscall->is_noreturn() && !syscall->kernel_return_type().IsVoid()) {
    writer->Printf(" %s", GetGoName(syscall->kernel_return_type()).c_str());
  }
  writer->Puts("\n");
}

}  // namespace

// This currrently handles both x86 and arm as they're identical.
bool GoSyscallsAsm(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  writer->Printf("#include \"textflag.h\"\n\n");

  for (const auto& syscall : library.syscalls()) {
    writer->Puts("// ");
    PrintStub(writer, syscall.get());
    writer->Printf("TEXT ·Sys_%s(SB),NOSPLIT,$0\n", syscall->snake_name().c_str());
    writer->Printf("\tJMP runtime·vdsoCall_zx_%s(SB)\n", syscall->snake_name().c_str());
    writer->Puts("\n");
  }

  return true;
}

bool GoSyscallsStubs(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  writer->Puts("package zx\n\n");
  writer->Puts("import \"unsafe\"\n\n");

  for (const auto& syscall : library.syscalls()) {
    writer->Puts("//go:noescape\n");
    writer->Puts("//go:nosplit\n");
    PrintStub(writer, syscall.get());
    writer->Puts("\n");
  }

  return true;
}
