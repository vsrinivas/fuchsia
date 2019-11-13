// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

bool VdsoWrappersOutput(const SyscallLibrary& library, Writer* writer) {
  if (!CopyrightHeaderWithCppComments(writer)) {
    return false;
  }

  for (const auto& syscall : library.syscalls()) {
    if (syscall->HasAttribute("vdsocall")) {
      continue;
    }

    // This function is specially handled by abigen.
    const bool is_test_wrapper = syscall->name() == "syscall_test_wrapper";

    if (!syscall->HasAttribute("blocking") && !is_test_wrapper) {
      continue;
    }

    constexpr const char indent[] = "    ";
    CSignatureLine(*syscall, "", "_zx_", writer, SignatureNewlineStyle::kAllOneLine, nullptr);
    writer->Printf(" {\n");
    writer->Printf("%s%s ret;\n", indent, GetCUserModeName(syscall->kernel_return_type()).c_str());
    if (is_test_wrapper) {
      writer->Printf("%sif (a < 0 || b < 0 || c < 0) return ZX_ERR_INVALID_ARGS;\n", indent);
    } else {
      writer->Printf("%sdo {\n", indent);
    }
    writer->Printf("%s%sret = SYSCALL_zx_%s(", indent, indent, syscall->name().c_str());
    for (size_t i = 0; i < syscall->kernel_arguments().size(); ++i) {
      const StructMember& arg = syscall->kernel_arguments()[i];
      writer->Puts(arg.name());
      if (i != syscall->kernel_arguments().size() - 1) {
        writer->Puts(", ");
      }
    }
    writer->Puts(");\n");
    if (is_test_wrapper) {
      writer->Printf("%sif (ret > 50) return ZX_ERR_OUT_OF_RANGE;\n", indent);
    } else {
      writer->Printf("%s} while (unlikely(ret == ZX_ERR_INTERNAL_INTR_RETRY));\n", indent);
    }
    writer->Printf("%sreturn ret;\n", indent);
    writer->Printf("}\n\n");

    writer->Printf("VDSO_INTERFACE_FUNCTION(zx_%s);\n\n", syscall->name().c_str());
  }

  return true;
}
