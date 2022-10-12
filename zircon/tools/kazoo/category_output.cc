// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

bool CategoryOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  const char* kCategories[] = {
      "blocking", "const", "next", "noreturn", "test_category1", "test_category2", "vdsocall",
  };

  for (const char* category : kCategories) {
    std::vector<std::string> syscalls_in_category;
    for (const auto& syscall : library.syscalls()) {
      if (syscall->HasAttribute(category)) {
        syscalls_in_category.push_back(syscall->snake_name());
      }
    }

    if (!syscalls_in_category.empty()) {
      std::string category_kernel_style = CamelToSnake(category);
      writer->Printf("#define HAVE_SYSCALL_CATEGORY_%s 1\n", category_kernel_style.c_str());
      writer->Printf("SYSCALL_CATEGORY_BEGIN(%s)\n", category_kernel_style.c_str());
      for (const auto& name : syscalls_in_category) {
        writer->Printf("    SYSCALL_IN_CATEGORY(%s)\n", name.c_str());
      }
      writer->Printf("SYSCALL_CATEGORY_END(%s)\n\n", category_kernel_style.c_str());
    }
  }

  return true;
}
