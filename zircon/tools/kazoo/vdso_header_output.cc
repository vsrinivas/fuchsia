// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

bool VdsoHeaderOutput(const SyscallLibrary& library, Writer* writer) {
  if (!CopyrightHeaderWithCppComments(writer)){
    return false;
  }

  for (const auto& syscall : library.syscalls()) {
    CDeclaration(*syscall, "__LOCAL extern ", "VDSO_zx_", writer);

    if (syscall->HasAttribute("vdsocall")) {
      continue;
    }

    CDeclaration(*syscall, "__LOCAL extern ", "SYSCALL_zx_", writer);
  }

  return true;
}
