// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/elf_symbol_record.h"

#include "llvm/Demangle/Demangle.h"

namespace zxdb {

ElfSymbolRecord::ElfSymbolRecord(uint64_t relative_address, const std::string& linkage_name)
    : relative_address(relative_address), linkage_name(linkage_name) {
  // TODO(brettw) use "demangled = llvm::demangle() when we roll LLVM. It avoids the buffer
  // allocation problem.
  int demangle_status = llvm::demangle_unknown_error;
  char* demangled_buf =
      llvm::itaniumDemangle(linkage_name.c_str(), nullptr, nullptr, &demangle_status);
  if (demangle_status == llvm::demangle_success) {
    unmangled_name = demangled_buf;
    free(demangled_buf);
  } else {
    // Fall back to the linkage name on error (might not be a mangled name at all).
    unmangled_name = linkage_name;
  }
}

}  // namespace zxdb
