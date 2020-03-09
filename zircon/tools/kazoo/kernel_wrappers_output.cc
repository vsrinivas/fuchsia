// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

bool KernelWrappersOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  writer->Puts("extern \"C\" {\n");

  constexpr const char indent[] = "    ";
  for (const auto& syscall : library.syscalls()) {
    if (syscall->HasAttribute("vdsocall")) {
      continue;
    }
    writer->Printf("syscall_result wrapper_%s(", syscall->name().c_str());
    for (const auto& arg : syscall->kernel_arguments()) {
      writer->Printf("%s %s, ", GetCUserModeName(arg.type()).c_str(), arg.name().c_str());
    }
    writer->Puts("uint64_t pc) {\n");
    writer->Printf(
        "%sreturn do_syscall(ZX_SYS_%s, pc, &VDso::ValidSyscallPC::%s, [&](ProcessDispatcher* "
        "current_process) -> uint64_t {\n",
        indent, syscall->name().c_str(), syscall->name().c_str());

    // Write out locals for the output handles.
    std::vector<std::string> out_handle_names;
    for (const auto& arg: syscall->kernel_arguments()) {
      const Type& type = arg.type();
      if (type.IsPointer()) {
        std::string pointed_to = GetCKernelModeName(type.DataAsPointer().pointed_to_type());
        if (type.constness() == Constness::kMutable && pointed_to == "zx_handle_t" &&
            !type.DataAsPointer().was_vector()) {
          out_handle_names.push_back(arg.name());
          writer->Printf("%s%suser_out_handle out_handle_%s;\n", indent, indent,
                         arg.name().c_str());
        }
      }
    }

    writer->Printf(
        "%s%s%s sys_%s(", indent, indent,
        syscall->is_noreturn() ? "/*noreturn*/" : "auto result =", syscall->name().c_str());
    for (size_t i = 0; i < syscall->kernel_arguments().size(); ++i) {
      const StructMember& arg = syscall->kernel_arguments()[i];
      const Type& type = arg.type();
      if (type.IsPointer()) {
        std::string pointed_to = GetCKernelModeName(type.DataAsPointer().pointed_to_type());
        if (type.constness() == Constness::kConst) {
          writer->Printf("make_user_in_ptr(%s)", arg.name().c_str());
        } else if (type.constness() == Constness::kMutable) {
          if (pointed_to == "zx_handle_t" && !type.DataAsPointer().was_vector()) {
            writer->Printf("&out_handle_%s", arg.name().c_str());
          } else if (type.optionality() == Optionality::kInputArgument) {
            writer->Printf("make_user_inout_ptr(%s)", arg.name().c_str());
          } else {
            writer->Printf("make_user_out_ptr(%s)", arg.name().c_str());
          }
        }
      } else {
        writer->Puts(arg.name());
      }
      if (i != syscall->kernel_arguments().size() - 1) {
        writer->Puts(", ");
      }
    }
    writer->Puts(");\n");

    // Complete copy out of output handles.
    if (!out_handle_names.empty()) {
      writer->Printf("%s%sif (result != ZX_OK)\n", indent, indent);
      writer->Printf("%s%s%sreturn result;\n", indent, indent, indent);

      for (const auto& name : out_handle_names) {
        writer->Printf(
            "%s%sif (out_handle_%s.begin_copyout(current_process, make_user_out_ptr(%s)))\n",
            indent, indent, name.c_str(), name.c_str());
        writer->Printf("%s%s%sreturn ZX_ERR_INVALID_ARGS;\n", indent, indent, indent);
      }

      for (const auto& name : out_handle_names) {
        writer->Printf("%s%sout_handle_%s.finish_copyout(current_process);\n", indent, indent,
                       name.c_str());
      }
    }

    if (syscall->is_noreturn()) {
      writer->Printf("%s%s/* NOTREACHED */\n", indent, indent);
      writer->Printf("%s%sreturn ZX_ERR_BAD_STATE;\n", indent, indent);
    } else {
      writer->Printf("%s%sreturn result;\n", indent, indent);
    }

    writer->Printf("%s});\n", indent);
    writer->Puts("}\n");
  }

  writer->Puts("}\n");

  return true;
}
