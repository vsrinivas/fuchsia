// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <utility>
#include "lib/fxl/logging.h"

namespace escher {

// TypeInfo is intended to be used in collaboration with TypedReffable.  It
// is a generic class that is parameterized by an enumeration that represents
// all types in a tree-shaped hierarchy (note: the total number of types in the
// hierarchy cannot exceed 65).
//
// For example, to represent a class hierarchy like:
// _ Base
//   |_ One
//   |_ Two
//      |_ SubTwo
//         |_ SubSubTwo
//
// ... you would start with an enumeration of all of these classes, and then
// create a TypeInfo to represent the inheritance chain of each class:
//
// enum class ExampleTypes {
//   kBase = 0,
//   kOne = 1,
//   kTwo = 1 << 1,
//   kSubTwo = 1 << 2,
//   kSubSubTwo = 1 << 3,
// };
//
// typedef TypeInfo<ExampleTypes> ExampleTypeInfo;
//
// ExampleTypeInfo base_info("Base", ExampleTypes::kBase);
// ExampleTypeInfo one_info("One", ExampleTypes::kBase,
//                                 ExampleTypes::kOne);
// ExampleTypeInfo two_info("Two", ExampleTypes::kBase,
//                                 ExampleTypes::kTwo);
// ExampleTypeInfo sub_two_info("SubTwo", ExampleTypes::kBase,
//                                        ExampleTypes::kTwo,
//                                        ExampleTypes::kSubTwo);
// ExampleTypeInfo sub_sub_two_info("SubSubTwo", ExampleTypes::kBase,
//                                               ExampleTypes::kTwo,
//                                               ExampleTypes::kSubTwo,
//                                               ExampleTypes::kSubSubTwo);
//
// You can now verify properties of the type-hierarchy:
//   EXPECT_FALSE(one_info.IsKindOf(two_info));
//   EXPECT_FALSE(two_info.IsKindOf(sub_two_info));
//   EXPECT_TRUE(sub_two_info.IsKindOf(two_info));
//
// In practice, these ExampleTypeInfos would not be stored in global variables,
// but instead as static members of the corresponding class.  For more details
// and examples, see typed_reffable.h and type_reffable_unittest.cc.
template <class EnumT>
struct TypeInfo {
  template <typename... Args>
  TypeInfo(const char* type_name, Args&&... args)
      : name(type_name), flags(FlattenTypeFlags(std::forward<Args>(args)...)) {}

  // The name of the class.
  const char* name;

  // Flattened bits for all enumerated types passed to the constructor.  Used
  // for comparing against other types.
  const uint64_t flags;

  // Return true if |base_type| represents a type that is the same or a base
  // type of the one represented by this TypeInfo.  Return false otherwise.
  bool IsKindOf(const TypeInfo<EnumT>& base_type) const {
    return (flags & base_type.flags) == base_type.flags;
  }

  // Return true if the two TypeInfos represent the same type.
  bool operator==(const TypeInfo<EnumT>& type) const {
    return flags == type.flags;
  }

 private:
  // Helper function for the TypeInfo constructor.  Flattens type flags by
  // performing a bitwise-or between the first argument, and the result of
  // flattening the rest of the arguments.  Verifies that there is no overlap
  // between bits in any of the arguments (this is a common way of shooting
  // oneself in the foot... but there are others that aren't caught).
  template <typename... Args>
  static uint64_t FlattenTypeFlags(EnumT type_flag, Args&&... rest) {
    auto this_flag = static_cast<uint64_t>(type_flag);
    auto other_flags = FlattenTypeFlags(std::forward<Args>(rest)...);
    // Bits in flags must not overlap.
    FXL_DCHECK((this_flag & other_flags) == 0);
    return this_flag | other_flags;
  }
  // Helper function for the TypeInfo constructor.  No type arg is specified,
  // so the flattened result is zero.
  static uint64_t FlattenTypeFlags() { return 0; }
};

}  // namespace escher
