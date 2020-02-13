// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_KAZOO_OUTPUTS_H_
#define TOOLS_KAZOO_OUTPUTS_H_

#include "tools/kazoo/syscall_library.h"
#include "tools/kazoo/writer.h"

// All of the output functions write only to |writer|, and return true on
// success or false on failure with an error logged.

bool AsmOutput(const SyscallLibrary& library, Writer* writer);
bool CategoryOutput(const SyscallLibrary& library, Writer* writer);
bool GoSyscallsAsm(const SyscallLibrary& library, Writer* writer);
bool GoSyscallsStubs(const SyscallLibrary& library, Writer* writer);
bool GoVdsoArm64Calls(const SyscallLibrary& library, Writer* writer);
bool GoVdsoKeys(const SyscallLibrary& library, Writer* writer);
bool GoVdsoX86Calls(const SyscallLibrary& library, Writer* writer);
bool JsonOutput(const SyscallLibrary& library, Writer* writer);
bool KernelBranchesOutput(const SyscallLibrary& library, Writer* writer);
bool KernelHeaderOutput(const SyscallLibrary& library, Writer* writer);
bool KernelWrappersOutput(const SyscallLibrary& library, Writer* writer);
bool KtraceOutput(const SyscallLibrary& library, Writer* writer);
bool RustOutput(const SyscallLibrary& library, Writer* writer);
bool SyscallNumbersOutput(const SyscallLibrary& library, Writer* writer);
bool UserHeaderOutput(const SyscallLibrary& library, Writer* writer);
bool VdsoHeaderOutput(const SyscallLibrary& library, Writer* writer);
bool VdsoWrappersOutput(const SyscallLibrary& library, Writer* writer);

#endif  // TOOLS_KAZOO_OUTPUTS_H_
