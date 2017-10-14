// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/base/reffable.h"

namespace escher {

// TypedReffable is a subclass of Reffable that supports dynamic type-checking.
// To make a new subclass of TypedReffable, you must follow some simple steps:
// 1) add a new entry to the enum of types in the hierarchy, creating a new enum
//    if this is the first type (see type_info.h for details).
// 2) in your new class, add a const static member to represent the type.
// 3) also in your new class, override type_info(), defining it to return a
//    reference to the static member.
//
// Following the example in type_info.h, assume that we are adding a new class
// named Foo.  We would first add this line to "enum class ExampleTypes":
//   kFoo = 1 << 4
//
// Then, we would define our class:
//
// class Foo : public TypedReffable<ExampleTypeInfo> {
//  public:
//   static const TypeInfo kTypeInfo;  // TypeInfo is typedeffed in superclass
//   const TypeInfo& type_info() const override { return kTypeInfo; }
// };
//
// For a working example, see typed_reffable_unittest.cc.
template <typename TypeInfoT>
class TypedReffable : public Reffable {
 public:
  typedef TypeInfoT TypeInfo;

  // Concrete subclasses must override this to return a type-specific TypeInfo.
  // For each subclass, the TypeInfo's:
  //   - type flags MUST match the inheritance chain of that subclass.
  //   - name MUST match the name of that subclass.
  virtual const TypeInfo& type_info() const = 0;

  // Return true if the specified type is identical or a base type of this
  // TypedReffable; return false otherwise.
  bool IsKindOf(const TypeInfo& base_type) const {
    return type_info().IsKindOf(base_type);
  }

  // Return true if the specified type is identical or a base type of this
  // TypedReffable; return false otherwise.
  template <typename TypedReffableT>
  bool IsKindOf() const {
    return type_info().IsKindOf(TypedReffableT::kTypeInfo);
  }

  // Return the name of the TypedReffable subclass that this object is an
  // instance of.
  const char* type_name() const { return type_info().name; }
};

}  // namespace escher
