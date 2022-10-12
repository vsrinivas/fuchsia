// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

namespace {

// Each incoming argument directly from the user is declared as using the
// widened type (always either int64_t or uint64_t) so the compiler is under no
// illusions that it can trust the incoming register values not to have excess
// high bit values set (or cleared for negative signed values).  Then the
// wrapper will safely narrow the register value into the argument value of the
// declared type.  See //zircon/kernel/lib/syscalls/safe-syscall-argument.h for
// the SafeSyscallArgument template class that provides the RawType type and
// the Sanitize function used in the generated code.

std::string ArgumentExpr(const StructMember& arg) {
  return "SafeSyscallArgument<" + GetCUserModeName(arg.type()) + ">::Sanitize(" + arg.name() + ")";
}

}  // namespace

bool KernelWrappersOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  writer->Puts("extern \"C\" {\n\n");

  constexpr const char indent[] = "    ";
  for (const auto& syscall : library.syscalls()) {
    if (syscall->HasAttribute("vdsocall")) {
      continue;
    }

    auto write_prototype = [&]() {
      writer->Printf("syscall_result wrapper_%s(", syscall->snake_name().c_str());
      for (const auto& arg : syscall->kernel_arguments()) {
        writer->Printf("SafeSyscallArgument<%s>::RawType %s, ",
                       GetCUserModeName(arg.type()).c_str(), arg.name().c_str());
      }
      writer->Puts("uint64_t pc)");
    };

    write_prototype();
    writer->Printf(";\n");

    write_prototype();
    writer->Printf(
        " {\n%sreturn do_syscall(ZX_SYS_%s, pc, &VDso::ValidSyscallPC::%s, [&](ProcessDispatcher* "
        "current_process) -> uint64_t {\n",
        indent, syscall->snake_name().c_str(), syscall->snake_name().c_str());

    // Write out locals for the output handles.
    std::vector<std::reference_wrapper<const StructMember>> out_handle_args;
    for (const auto& arg : syscall->kernel_arguments()) {
      const Type& type = arg.type();
      if (type.IsPointer()) {
        std::string pointed_to = GetCKernelModeName(type.DataAsPointer().pointed_to_type());
        if (type.constness() == Constness::kMutable && pointed_to == "zx_handle_t" &&
            !type.DataAsPointer().was_vector()) {
          out_handle_args.push_back(arg);
          writer->Printf("%s%suser_out_handle out_handle_%s;\n", indent, indent,
                         arg.name().c_str());
        }
      }
    }

    writer->Printf(
        "%s%s%s sys_%s(", indent, indent,
        syscall->is_noreturn() ? "/*noreturn*/" : "auto result =", syscall->snake_name().c_str());
    for (size_t i = 0; i < syscall->kernel_arguments().size(); ++i) {
      const StructMember& arg = syscall->kernel_arguments()[i];
      const Type& type = arg.type();
      const std::string arg_expr = ArgumentExpr(arg);
      if (type.IsPointer()) {
        std::string pointed_to = GetCKernelModeName(type.DataAsPointer().pointed_to_type());
        if (type.constness() == Constness::kConst) {
          writer->Printf("make_user_in_ptr(%s)", arg_expr.c_str());
        } else if (type.constness() == Constness::kMutable) {
          if (pointed_to == "zx_handle_t" && !type.DataAsPointer().was_vector()) {
            writer->Printf("&out_handle_%s", arg.name().c_str());
          } else if (type.optionality() == Optionality::kInputArgument) {
            writer->Printf("make_user_inout_ptr(%s)", arg_expr.c_str());
          } else {
            writer->Printf("make_user_out_ptr(%s)", arg_expr.c_str());
          }
        }
      } else {
        writer->Puts(arg_expr);
      }
      if (i != syscall->kernel_arguments().size() - 1) {
        writer->Puts(", ");
      }
    }
    writer->Puts(");\n");

    // Complete copy out of output handles.
    if (!out_handle_args.empty()) {
      writer->Printf("%s%sif (result != ZX_OK)\n", indent, indent);
      writer->Printf("%s%s%sreturn result;\n", indent, indent, indent);

      for (const StructMember& arg : out_handle_args) {
        writer->Printf(
            "%s%sresult = out_handle_%s.begin_copyout(current_process, make_user_out_ptr(%s));\n",
            indent, indent, arg.name().c_str(), ArgumentExpr(arg).c_str());
        writer->Printf("%s%sif (result != ZX_OK)\n", indent, indent);
        writer->Printf("%s%s%sreturn result;\n", indent, indent, indent);
      }

      for (const StructMember& arg : out_handle_args) {
        writer->Printf("%s%sout_handle_%s.finish_copyout(current_process);\n", indent, indent,
                       arg.name().c_str());
      }
    }

    if (syscall->is_noreturn()) {
      writer->Printf("%s%s/* NOTREACHED */\n", indent, indent);
      writer->Printf("%s%sreturn ZX_ERR_BAD_STATE;\n", indent, indent);
    } else {
      writer->Printf("%s%sreturn result;\n", indent, indent);
    }

    writer->Printf("%s});\n", indent);
    writer->Puts("}\n\n");
  }

  writer->Puts("}\n");

  return true;
}
