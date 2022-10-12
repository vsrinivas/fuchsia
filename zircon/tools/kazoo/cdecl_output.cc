// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"

using namespace std::literals;

namespace {

constexpr std::pair<const char*, std::string_view> kFunctionAttributes[] = {
    {"const", "__CONST"},
};

constexpr std::pair<std::string_view, std::string_view> kHandleAttributes[] = {
    {"acquire", "acquire_handle"},
    {"release", "release_handle"},
    {"use", "use_handle"},
};

bool IsHandleType(const Type& type) {
  if (type.IsPointer()) {
    return IsHandleType(type.DataAsPointer().pointed_to_type());
  }
  if (type.IsVector()) {
    return IsHandleType(type.DataAsVector().contained_type());
  }
  if (type.IsStruct()) {
    const auto& s = type.DataAsStruct().struct_data();
    return std::any_of(s.members().begin(), s.members().end(),
                       [](const auto& m) { return IsHandleType(m.type()); });
  }
  return type.IsHandle();
}

std::optional<std::string_view> HandleAnnotation(const StructMember& arg) {
  if (IsHandleType(arg.type())) {
    for (const auto& [attr, anno] : kHandleAttributes) {
      if (arg.attributes().count(std::string(attr))) {
        return anno;
      }
    }
    switch (arg.type().optionality()) {
      case Optionality::kOutputOptional:
      case Optionality::kOutputNonOptional:
        return "acquire_handle"sv;
      default:
        return "use_handle"sv;
    }
  }
  return {};
}

void CDeclarationMacro(const Syscall& syscall, std::string_view macro,
                       std::string (*type_name)(const Type&), Writer* writer) {
  std::string decl(macro);
  decl += "(";
  decl += syscall.snake_name();
  decl += ", ";

  // First the return type.
  decl += type_name(syscall.kernel_return_type());
  decl += ",";

  {
    // Now the function attributes.
    std::string attrs;

    if (syscall.is_noreturn()) {
      attrs += " __NO_RETURN";
    }
    for (const auto& [attr, anno] : kFunctionAttributes) {
      if (syscall.HasAttribute(attr)) {
        attrs += " ";
        attrs += anno;
      }
    }

    if (attrs.empty()) {
      decl += " /* no attributes */";
    } else {
      decl += attrs;
    }
  }
  decl += ", ";

  // Now the argument count, used in assembly macros.
  decl += std::to_string(syscall.num_kernel_args());
  decl += ",\n    ";

  {
    // Now the argument list, just the names between parentheses.
    decl += "(";
    bool first = true;
    for (const auto& arg : syscall.kernel_arguments()) {
      if (!first) {
        decl += ", ";
      }
      first = false;
      decl += arg.name();
    }
    decl += ")";
  }
  decl += ", ";

  // Finally, the full prototype.
  if (syscall.kernel_arguments().empty()) {
    decl += "(void)";
  } else {
    const bool unchecked = syscall.HasAttribute("HandleUnchecked");
    bool first = true;
    for (const auto& arg : syscall.kernel_arguments()) {
      decl += first ? "("sv : ","sv;
      first = false;
      decl += "\n    ";

      auto anno = HandleAnnotation(arg);
      if (anno) {
        decl += "_ZX_SYSCALL_ANNO(";
        decl += *anno;
        decl += unchecked ? "(\"FuchsiaUnchecked\")"sv : "(\"Fuchsia\")"sv;
        decl += ") ";
      }

      decl += type_name(arg.type());
      decl += " ";
      decl += arg.name();
    }
    decl += ")";
  }

  decl += ")\n\n";

  writer->Puts(decl.c_str());
}

constexpr std::string_view PrivateMacro(const Syscall& syscall) {
  if (syscall.HasAttribute("vdsocall"))
    return "VDSO_SYSCALL";
  if (syscall.HasAttribute("blocking"))
    return "BLOCKING_SYSCALL";
  if (syscall.HasAttribute("internal"))
    return "INTERNAL_SYSCALL";
  return "KERNEL_SYSCALL";
}

}  // namespace

bool PublicDeclarationsOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  writer->Puts(R"(#ifndef _ZX_SYSCALL_DECL
#error "<zircon/syscalls.h> is the public API header"
#endif

)");

  for (const auto& syscall : library.syscalls()) {
    if (!syscall->HasAttribute("internal") && !syscall->HasAttribute("testonly") &&
        !syscall->HasAttribute("next")) {
      CDeclarationMacro(*syscall, "_ZX_SYSCALL_DECL", GetCUserModeName, writer);
    }
  }

  return true;
}

bool NextPublicDeclarationsOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  writer->Puts(R"(#ifndef _ZX_SYSCALL_DECL
#error "<zircon/syscalls-next.h> is the public API header"
#endif

)");

  for (const auto& syscall : library.syscalls()) {
    if (!syscall->HasAttribute("internal") && syscall->HasAttribute("next")) {
      CDeclarationMacro(*syscall, "_ZX_SYSCALL_DECL", GetCUserModeName, writer);
    }
  }

  return true;
}

bool TestonlyPublicDeclarationsOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  writer->Puts(R"(#ifndef _ZX_SYSCALL_DECL
#error "<zircon/testonly-syscalls.h> is the public API header"
#endif

)");

  for (const auto& syscall : library.syscalls()) {
    if (!syscall->HasAttribute("internal") && syscall->HasAttribute("testonly")) {
      CDeclarationMacro(*syscall, "_ZX_SYSCALL_DECL", GetCUserModeName, writer);
    }
  }

  return true;
}

bool PrivateDeclarationsOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  for (const auto& syscall : library.syscalls()) {
    CDeclarationMacro(*syscall, PrivateMacro(*syscall), GetCUserModeName, writer);
  }

  return true;
}

bool KernelDeclarationsOutput(const SyscallLibrary& library, Writer* writer) {
  CopyrightHeaderWithCppComments(writer);

  for (const auto& syscall : library.syscalls()) {
    CDeclarationMacro(*syscall, PrivateMacro(*syscall), GetCKernelModeName, writer);
  }

  return true;
}
