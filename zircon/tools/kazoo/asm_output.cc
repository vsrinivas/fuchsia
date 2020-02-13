// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

// This currently handles both x86 and arm asm output, as they're identical.
bool AsmOutput(const SyscallLibrary& library, Writer* writer) {
  if (!CopyrightHeaderWithCppComments(writer)){
    return false;
  }

  size_t i = 0;
  for (const auto& syscall : library.syscalls()) {
    if (syscall->HasAttribute("vdsocall")) {
      continue;
    }

    bool is_private = syscall->HasAttribute("blocking") || syscall->HasAttribute("internal") ||
                     syscall->name() == "syscall_test_wrapper";  // This is hardcoded in abigen.
    writer->Printf("m_syscall zx_%s %zu %zu %d\n", syscall->name().c_str(), i,
                   syscall->num_kernel_args(), is_private ? 0 : 1);
    ++i;
  }

  return true;
}
