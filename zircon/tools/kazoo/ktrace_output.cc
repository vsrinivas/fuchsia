// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

bool KtraceOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  size_t i = 0;
  for (const auto& syscall : library.syscalls()) {
    if (syscall->HasAttribute("vdsocall")) {
      continue;
    }
    writer->Printf("{%zu, %zu, \"%s\"},\n", i, syscall->num_kernel_args(),
                   syscall->snake_name().c_str());
    ++i;
  }

  return true;
}
