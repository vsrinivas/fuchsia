// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

bool KtraceOutput(const SyscallLibrary& library, Writer* writer) {
  if (!CopyrightHeaderWithCppComments(writer)){
    return false;
  }

  size_t i = 0;
  for (const auto& syscall : library.syscalls()) {
    if (syscall->HasAttribute("Vdsocall")) {
      continue;
    }
    writer->Printf("{%zu, %zu, \"%s\"},\n", i, syscall->NumKernelArgs(), syscall->name().c_str());
    ++i;
  }

  // TODO(syscall-fidl-transition): Original file has an extra \n, add one here
  // for consistency.
  writer->Puts("\n");

  return true;
}
