// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

bool SyscallNumbersOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  size_t i = 0;
  for (const auto& syscall : library.syscalls()) {
    if (syscall->HasAttribute("vdsocall")) {
      continue;
    }
    writer->Printf("#define ZX_SYS_%s %zu\n", syscall->snake_name().c_str(), i);
    ++i;
  }

  writer->Printf("#define ZX_SYS_COUNT %zu\n", i);

  return true;
}
