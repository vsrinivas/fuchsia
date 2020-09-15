// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/code_generator/cpp_visitor.h"

namespace fidl_codec {

void CppVariableStruct::GenerateInitialization(PrettyPrinter& printer, const char* suffix) const {
  /*
   * Given the following FIDL definition:
   *
   * struct Color {
   *     uint32 id;
   *     string:MAX_STRING_LENGTH name = "red";
   * };
   *
   * We are interested in generating the following HLCPP code:
   *
   * fuchsia::examples::Color blue = {1, "blue"};
   *
   * (See https://fuchsia.dev/fuchsia-src/reference/fidl/bindings/hlcpp-bindings#structs)
   */

  std::vector<std::shared_ptr<CppVariable>> struct_members;

  for (const std::unique_ptr<fidl_codec::StructMember>& struct_member :
       value()->AsStructValue()->struct_definition().members()) {
    const fidl_codec::Value* member_value =
        value()->AsStructValue()->GetFieldValue(struct_member->name());

    CppVisitor visitor(name() + "_" + struct_member->name());
    member_value->Visit(&visitor, struct_member->type());

    auto member = visitor.result();
    member->GenerateInitialization(printer);

    struct_members.emplace_back(member);
  }

  GenerateTypeAndName(printer);
  printer << " = { ";
  std::string separator;
  for (const auto& member : struct_members) {
    printer << separator;
    separator = ", ";
    member->GenerateName(printer);
  }
  printer << " };\n";
}

}  // namespace fidl_codec
