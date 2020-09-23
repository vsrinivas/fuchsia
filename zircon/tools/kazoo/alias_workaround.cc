// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/syscall_library.h"

// See alias_workarounds[.test].fidl for explanation and what these will be in "real" .fidl once the
// frontend supports the necessary syntax.
bool AliasWorkaround(const std::string& name, const SyscallLibrary& library, Type* type) {
  if (name == "charptr") {
    *type = Type(TypePointer(Type(TypeChar{})), Constness::kMutable);
    return true;
  }
  if (name == "const_futexptr") {
    *type = Type(TypePointer(Type(TypeZxBasicAlias("futex"))), Constness::kConst);
    return true;
  }
  if (name == "const_voidptr") {
    *type = Type(TypePointer(Type(TypeVoid{})), Constness::kConst);
    return true;
  }
  if (name == "mutable_string") {
    *type = Type(TypeString{}, Constness::kMutable);
    return true;
  }
  if (name == "mutable_uint32") {
    *type = Type(TypePointer(Type(TypeUint32{})), Constness::kMutable);
    return true;
  }
  if (name == "mutable_usize") {
    *type = Type(TypePointer(Type(TypeSizeT{})), Constness::kMutable);
    return true;
  }
  if (name == "mutable_vector_HandleDisposition_u32size") {
    *type = Type(TypeVector(Type(library.TypeFromIdentifier("zx/HandleDisposition")),
                            UseUint32ForVectorSizeTag{}),
                 Constness::kMutable);
    return true;
  }
  if (name == "mutable_vector_HandleInfo_u32size") {
    *type = Type(
        TypeVector(Type(library.TypeFromIdentifier("zx/HandleInfo")), UseUint32ForVectorSizeTag{}),
        Constness::kMutable);
    return true;
  }
  if (name == "mutable_ChannelCallEtcArgs") {
    *type = Type(TypePointer(Type(library.TypeFromIdentifier("zx/ChannelCallEtcArgs"))),
                 Constness::kMutable);
    return true;
  }
  if (name == "mutable_vector_WaitItem") {
    *type = Type(TypeVector(Type(library.TypeFromIdentifier("zx/WaitItem"))), Constness::kMutable);
    return true;
  }
  if (name == "mutable_vector_handle_u32size") {
    *type = Type(TypeVector(Type(TypeHandle(std::string())), UseUint32ForVectorSizeTag{}),
                 Constness::kMutable);
    return true;
  }
  if (name == "mutable_vector_void") {
    *type = Type(TypeVector(Type(TypeVoid{})), Constness::kMutable);
    return true;
  }
  if (name == "mutable_vector_void_u32size") {
    *type = Type(TypeVector(Type(TypeVoid{}), UseUint32ForVectorSizeTag{}), Constness::kMutable);
    return true;
  }
  if (name == "optional_PciBar") {
    *type = Type(library.TypeFromIdentifier("zx/PciBar").type_data(), Constness::kUnspecified,
                 Optionality::kOutputOptional);
    return true;
  }
  if (name == "optional_PortPacket") {
    *type = Type(library.TypeFromIdentifier("zx/PortPacket").type_data(), Constness::kUnspecified,
                 Optionality::kOutputOptional);
    return true;
  }
  if (name == "optional_koid") {
    *type = Type(TypeZxBasicAlias("koid"), Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "optional_signals") {
    *type =
        Type(TypeZxBasicAlias("signals"), Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "optional_time") {
    *type = Type(TypeZxBasicAlias("time"), Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "optional_uint32") {
    *type = Type(TypeUint32{}, Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "optional_usize") {
    *type = Type(TypeSizeT{}, Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "optional_off") {
    *type = Type(TypeZxBasicAlias("off"), Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "vector_HandleInfo_u32size") {
    *type = Type(
        TypeVector(Type(library.TypeFromIdentifier("zx/HandleInfo")), UseUint32ForVectorSizeTag{}),
        Constness::kConst);
    return true;
  }
  if (name == "vector_handle_u32size") {
    *type = Type(TypeVector(Type(TypeHandle(std::string())), UseUint32ForVectorSizeTag{}),
                 Constness::kConst);
    return true;
  }
  if (name == "vector_paddr") {
    *type = Type(TypeVector(Type(TypeZxBasicAlias("paddr"))), Constness::kConst);
    return true;
  }
  if (name == "vector_void") {
    *type = Type(TypeVector(Type(TypeVoid{})), Constness::kConst);
    return true;
  }
  if (name == "vector_iovec") {
    *type = Type(TypeVector(Type(TypeZxBasicAlias("iovec"))), Constness::kConst);
    return true;
  }
  if (name == "vector_void_u32size") {
    *type = Type(TypeVector(Type(TypeVoid{}), UseUint32ForVectorSizeTag{}), Constness::kConst);
    return true;
  }
  if (name == "voidptr") {
    *type = Type(TypePointer(Type(TypeVoid{})), Constness::kMutable);
    return true;
  }
  if (name == "string_view") {
    *type = Type(TypeZxBasicAlias("string_view"));
    return true;
  }
  return false;
}
