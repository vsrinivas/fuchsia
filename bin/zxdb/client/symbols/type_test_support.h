// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/type.h"

namespace zxdb {

class BaseType;
class StructClass;

// Returns a type that can hold 4-byte signed integers.
fxl::RefPtr<BaseType> MakeInt32Type();

// Defines a structure with two members of the given name and type. The members
// will immediately follow each other in memory.
fxl::RefPtr<StructClass> MakeStruct2Members(
    const std::string& struct_name, fxl::RefPtr<Type> member_1_type,
    const std::string& member_1_name, fxl::RefPtr<Type> member_2_type,
    const std::string& member_2_name);

}  // namespace zxdb
