// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_OBJECT_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_OBJECT_H_

#include "../type_shape.h"

namespace fidl {
namespace flat {

// An |Object| is anything that can be encoded in the FIDL wire format. Thus, all objects have
// information such as as their size, alignment, and depth (how many levels of sub-objects are
// contained within an object). See the FIDL wire format's definition of "object" for more details.
// TODO(fxbug.dev/37535): Remove this Object class, since it forms a third type hierarchy along with Type
// & Decl.
struct Object {
  virtual ~Object() = default;

  TypeShape typeshape(fidl::WireFormat wire_format) const { return TypeShape(*this, wire_format); }

  // |Visitor|, and the corresponding |Accept()| method below, enable the visitor pattern to be used
  // for derived classes of Object. See <https://en.wikipedia.org/wiki/Visitor_pattern> for
  // background on the visitor pattern. Versus a textbook visitor pattern:
  //
  // * Visitor enables a value to be returned to the caller of Accept(): Visitor's template type |T|
  //   is the type of the return value.
  //
  // * A Visitor's Visit() method returns an std::any. Visit() is responsible for returning a
  //   std::any with the correct type |T| for its contained value; otherwise, an any_cast exception
  //   will occur when the resulting std::any is any_casted back to |T| by Accept(). However, the
  //   client API that uses a visitor via Accept() will have guaranteed type safety.
  //
  // The use of std::any is an explicit design choice. It's possible to have a visitor
  // implementation that can completely retain type safety, but the use of std::any leads to a more
  // straightforward, ergonomic API than a solution involving heavy template metaprogramming.
  //
  // Implementation details: Visitor<T> is derived from VisitorAny, which achieves type-erasure via
  // std::any. Internally, only the type-erased VisitorAny class is used, along with a non-public
  // AcceptAny() method. The public Visitor<T> class and Accept<T> methods are small wrappers around
  // the internal type-erased versions. See
  // <https://eli.thegreenplace.net/2018/type-erasure-and-reification/> for a good introduction to
  // type erasure in C++.
  //
  // TODO(fxbug.dev/37535): Refactor the visitor pattern here to be the simpler kind-enum + switch()
  // dispatch.
  template <typename T>
  struct Visitor;

  template <typename T>
  T Accept(Visitor<T>* visitor) const;

 protected:
  struct VisitorAny;
  virtual std::any AcceptAny(VisitorAny* visitor) const = 0;
};

}  // namespace flat
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_OBJECT_H_
