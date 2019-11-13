// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include "tools/kazoo/output_util.h"
#include "tools/kazoo/outputs.h"
#include "tools/kazoo/string_util.h"

namespace {

std::string RustName(const Type& type) {
  struct {
   public:
    void operator()(const std::monostate&) { ret = "<TODO!>"; }
    void operator()(const TypeBool&) { ret = "bool"; }
    void operator()(const TypeChar&) { ret = "u8"; }
    void operator()(const TypeInt32&) { ret = "i32"; }
    void operator()(const TypeInt64&) { ret = "i64"; }
    void operator()(const TypeSizeT&) { ret = "usize"; }
    void operator()(const TypeUint16&) { ret = "u16"; }
    void operator()(const TypeUint32&) { ret = "u32"; }
    void operator()(const TypeUint64&) { ret = "u64"; }
    void operator()(const TypeUint8&) { ret = "u8"; }
    void operator()(const TypeVoid&) { ret = "u8"; }
    // TODO(syscall-fidl-transition): This is what abigen does, not sure if there's something better
    // that could be done.
    void operator()(const TypeUintptrT&) { ret = "usize"; }
    void operator()(const TypeZxBasicAlias& zx_basic_alias) { ret = zx_basic_alias.name(); }

    void operator()(const TypeEnum& enm) { ret = enm.enum_data().name(); }
    void operator()(const TypeHandle& handle) {
      ret = "zx_handle_t";
      // TOOD(syscall-fidl-transition): Once we're not trying to match abigen, it might be nice to
      // add the underlying handle type here like "zx_handle_t /*vmo*/" or similar.
    }
    void operator()(const TypePointer& pointer) {
      ret = StringPrintf("*%s %s", constness == Constness::kConst ? "const" : "mut",
                         RustName(pointer.pointed_to_type()).c_str());
    }
    void operator()(const TypeString&) {
      ZX_ASSERT(false && "can't convert string directly");
      ret = "<!>";
    }
    void operator()(const TypeStruct& strukt) { ret = strukt.struct_data().name(); }
    void operator()(const TypeVector&) {
      ZX_ASSERT(false && "can't convert vector directly");
      ret = "<!>";
    }

    Constness constness;
    std::string ret;
  } name_visitor;
  name_visitor.constness = type.constness();
  std::visit(name_visitor, type.type_data());
  return name_visitor.ret;
}

}  // namespace

bool RustOutput(const SyscallLibrary& library, Writer* writer) {
  if (!CopyrightHeaderWithCppComments(writer)) {
    return false;
  }

  constexpr const char indent[] = "    ";
  writer->Puts("#[link(name = \"zircon\")]\n");
  writer->Puts("extern {\n");
  for (const auto& syscall : library.syscalls()) {
    if (syscall->HasAttribute("internal")) {
      continue;
    }

    writer->Printf("%spub fn zx_%s(\n", indent, syscall->name().c_str());
    for (size_t i = 0; i < syscall->kernel_arguments().size(); ++i) {
      const StructMember& arg = syscall->kernel_arguments()[i];
      const bool last = i == syscall->kernel_arguments().size() - 1;
      writer->Printf("%s%s%s: %s%s\n", indent, indent, arg.name().c_str(),
                     RustName(arg.type()).c_str(), last ? "" : ",");
    }
    writer->Printf("%s%s)", indent, indent);
    if (!syscall->kernel_return_type().IsVoid()) {
      writer->Printf(" -> %s", RustName(syscall->kernel_return_type()).c_str());
    }
    writer->Puts(";\n\n");
  }
  writer->Puts("}\n");

  return true;
}
