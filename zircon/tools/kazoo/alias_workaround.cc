// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/syscall_library.h"

// See alias_workarounds[.test].fidl for explanation and what these will be in "real" .fidl once the
// frontend supports the necessary syntax.
bool AliasWorkaround(const std::string& name, const SyscallLibrary& library, Type* type) {
  if (name == "ConstFutexPtr") {
    *type = Type(TypePointer(Type(TypeZxBasicAlias("Futex"))), Constness::kConst);
    return true;
  }
  if (name == "ConstVoidPtr") {
    *type = Type(TypePointer(Type(TypeVoid{})), Constness::kConst);
    return true;
  }
  if (name == "MutableString") {
    *type = Type(TypeString{}, Constness::kMutable);
    return true;
  }
  if (name == "MutableUint32") {
    *type = Type(TypePointer(Type(TypeUint32{})), Constness::kMutable);
    return true;
  }
  if (name == "MutableUsize") {
    *type = Type(TypePointer(Type(TypeSizeT{})), Constness::kMutable);
    return true;
  }
  if (name == "MutableVectorHandleDispositionU32Size") {
    *type = Type(TypeVector(Type(library.TypeFromIdentifier("zx/HandleDisposition")),
                            UseUint32ForVectorSizeTag{}),
                 Constness::kMutable);
    return true;
  }
  if (name == "MutableVectorHandleInfoU32Size") {
    *type = Type(
        TypeVector(Type(library.TypeFromIdentifier("zx/HandleInfo")), UseUint32ForVectorSizeTag{}),
        Constness::kMutable);
    return true;
  }
  if (name == "MutableChannelCallEtcArgs") {
    *type = Type(TypePointer(Type(library.TypeFromIdentifier("zx/ChannelCallEtcArgs"))),
                 Constness::kMutable);
    return true;
  }
  if (name == "MutableVectorWaitItem") {
    *type = Type(TypeVector(Type(library.TypeFromIdentifier("zx/WaitItem"))), Constness::kMutable);
    return true;
  }
  if (name == "MutableVectorHandleU32Size") {
    *type = Type(TypeVector(Type(TypeHandle(std::string())), UseUint32ForVectorSizeTag{}),
                 Constness::kMutable);
    return true;
  }
  if (name == "MutableVectorVoid") {
    *type = Type(TypeVector(Type(TypeVoid{})), Constness::kMutable);
    return true;
  }
  if (name == "MutableVectorVoidU32Size") {
    *type = Type(TypeVector(Type(TypeVoid{}), UseUint32ForVectorSizeTag{}), Constness::kMutable);
    return true;
  }
  if (name == "OptionalPciBar") {
    *type = Type(library.TypeFromIdentifier("zx/PciBar").type_data(), Constness::kUnspecified,
                 Optionality::kOutputOptional);
    return true;
  }
  if (name == "OptionalPortPacket") {
    *type = Type(library.TypeFromIdentifier("zx/PortPacket").type_data(), Constness::kUnspecified,
                 Optionality::kOutputOptional);
    return true;
  }
  if (name == "OptionalKoid") {
    *type = Type(TypeZxBasicAlias("koid"), Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "OptionalSignals") {
    *type =
        Type(TypeZxBasicAlias("Signals"), Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "OptionalTime") {
    *type = Type(TypeZxBasicAlias("time"), Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "OptionalUint32") {
    *type = Type(TypeUint32{}, Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "OptionalUsize") {
    *type = Type(TypeSizeT{}, Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "OptionalOff") {
    *type = Type(TypeZxBasicAlias("off"), Constness::kUnspecified, Optionality::kOutputOptional);
    return true;
  }
  if (name == "VectorHandleInfoU32Size") {
    *type = Type(
        TypeVector(Type(library.TypeFromIdentifier("zx/HandleInfo")), UseUint32ForVectorSizeTag{}),
        Constness::kConst);
    return true;
  }
  if (name == "VectorHandleU32Size") {
    *type = Type(TypeVector(Type(TypeHandle(std::string())), UseUint32ForVectorSizeTag{}),
                 Constness::kConst);
    return true;
  }
  if (name == "VectorPaddr") {
    *type = Type(TypeVector(Type(TypeZxBasicAlias("paddr"))), Constness::kConst);
    return true;
  }
  if (name == "VectorVoid") {
    *type = Type(TypeVector(Type(TypeVoid{})), Constness::kConst);
    return true;
  }
  if (name == "VectorIovec") {
    *type = Type(TypeVector(Type(TypeZxBasicAlias("Iovec"))), Constness::kConst);
    return true;
  }
  if (name == "VectorVoidU32Size") {
    *type = Type(TypeVector(Type(TypeVoid{}), UseUint32ForVectorSizeTag{}), Constness::kConst);
    return true;
  }
  if (name == "VoidPtr") {
    *type = Type(TypePointer(Type(TypeVoid{})), Constness::kMutable);
    return true;
  }
  if (name == "StringView") {
    *type = Type(TypeZxBasicAlias("StringView"));
    return true;
  }
  return false;
}
