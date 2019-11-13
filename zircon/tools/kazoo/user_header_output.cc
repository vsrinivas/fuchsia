// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

bool UserHeaderOutput(const SyscallLibrary& library, Writer* writer) {
  if (!CopyrightHeaderWithCppComments(writer)) {
    return false;
  }

  for (const auto& syscall : library.syscalls()) {
    if (syscall->HasAttribute("internal")) {
      continue;
    }

    CDeclaration(*syscall, "__EXPORT extern ", "zx_", writer);
    CDeclaration(*syscall, "__EXPORT extern ", "_zx_", writer);
  }

  return true;
}
