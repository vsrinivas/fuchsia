// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

namespace {

void Declaration(Writer* writer, const char* name_prefix, const Syscall& syscall) {
  writer->Printf("__LOCAL extern ");
  writer->Printf("%s ", GetCName(syscall.kernel_return_type()).c_str());
  writer->Printf("%s_zx_%s(\n", name_prefix, syscall.name().c_str());

  std::vector<std::string> non_nulls;
  if (syscall.kernel_arguments().size() == 0) {
    writer->Printf("    void");
  } else {
    for (size_t i = 0; i < syscall.kernel_arguments().size(); ++i) {
      const StructMember& arg = syscall.kernel_arguments()[i];
      const bool last = i == syscall.kernel_arguments().size() - 1;
      writer->Printf("    %s %s%s", GetCName(arg.type()).c_str(), arg.name().c_str(),
                     last ? "" : ",\n");
      if (arg.type().IsPointer() && arg.type().optionality() == Optionality::kOutputNonOptional) {
        non_nulls.push_back(fxl::StringPrintf("%zu", i + 1));
      }
    }
  }
  writer->Printf(")");

  // TODO(syscall-fidl-transition): The order of these post-declaration markup is maintained, but
  // perhaps it could be simplified once it doesn't need to match.

  if (!non_nulls.empty()) {
    // TODO(syscall-fidl-transition): abigen only tags non-optional arguments as non-null, but
    // other input pointers could also perhaps be usefully tagged as well.
    writer->Printf(" __NONNULL((%s))", fxl::JoinStrings(non_nulls, ", ").c_str());
  }
  writer->Printf(" __LEAF_FN");
  if (syscall.HasAttribute("Const")) {
    writer->Printf(" __CONST");
  }
  if (syscall.HasAttribute("Noreturn")) {
    writer->Printf(" __NO_RETURN");
  }
  writer->Printf(";\n\n");
}

}  // namespace

bool VdsoHeaderOutput(const SyscallLibrary& library, Writer* writer) {
  if (!CopyrightHeaderWithCppComments(writer)){
    return false;
  }

  for (const auto& syscall : library.syscalls()) {
    Declaration(writer, "VDSO", *syscall);

    if (syscall->HasAttribute("Vdsocall")) {
      continue;
    }

    Declaration(writer, "SYSCALL", *syscall);
  }

  // TODO(syscall-fidl-transition): Original file has an extra \n, add one here
  // for consistency.
  writer->Puts("\n");

  return true;
}
