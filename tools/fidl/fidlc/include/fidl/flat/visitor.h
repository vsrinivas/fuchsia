// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VISITOR_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VISITOR_H_

#include "tools/fidl/fidlc/include/fidl/flat/object.h"
#include "tools/fidl/fidlc/include/fidl/flat/types.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"

namespace fidl::flat {

// See the comment on Object::Visitor<T> for more details.
struct Object::VisitorAny {
  virtual std::any Visit(const ArrayType&) = 0;
  virtual std::any Visit(const VectorType&) = 0;
  virtual std::any Visit(const StringType&) = 0;
  virtual std::any Visit(const HandleType&) = 0;
  virtual std::any Visit(const PrimitiveType&) = 0;
  virtual std::any Visit(const InternalType&) = 0;
  virtual std::any Visit(const IdentifierType&) = 0;
  virtual std::any Visit(const TransportSideType&) = 0;
  virtual std::any Visit(const BoxType&) = 0;
  virtual std::any Visit(const Enum&) = 0;
  virtual std::any Visit(const Bits&) = 0;
  virtual std::any Visit(const NewType&) = 0;
  virtual std::any Visit(const Service&) = 0;
  virtual std::any Visit(const Struct&) = 0;
  virtual std::any Visit(const Struct::Member&) = 0;
  virtual std::any Visit(const Table&) = 0;
  virtual std::any Visit(const Table::Member&) = 0;
  virtual std::any Visit(const Table::Member::Used&) = 0;
  virtual std::any Visit(const Union&) = 0;
  virtual std::any Visit(const Union::Member&) = 0;
  virtual std::any Visit(const Union::Member::Used&) = 0;
  virtual std::any Visit(const Protocol&) = 0;
  virtual std::any Visit(const ZxExperimentalPointerType&) = 0;
};

// This Visitor<T> class is useful so that Object.Accept() can enforce that its return type
// matches the template type of Visitor. See the comment on Object::Visitor<T> for more
// details.
template <typename T>
struct Object::Visitor : public VisitorAny {};

template <typename T>
T Object::Accept(Visitor<T>* visitor) const {
  return std::any_cast<T>(AcceptAny(visitor));
}

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VISITOR_H_
