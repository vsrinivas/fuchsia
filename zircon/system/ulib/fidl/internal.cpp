// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

namespace fidl {

namespace internal {

// Coding tables for primitives are predefined and interned here.
const fidl_type kBoolTable(FidlCodedPrimitive::kBool);
const fidl_type kInt8Table(FidlCodedPrimitive::kInt8);
const fidl_type kInt16Table(FidlCodedPrimitive::kInt16);
const fidl_type kInt32Table(FidlCodedPrimitive::kInt32);
const fidl_type kInt64Table(FidlCodedPrimitive::kInt64);
const fidl_type kUint8Table(FidlCodedPrimitive::kUint8);
const fidl_type kUint16Table(FidlCodedPrimitive::kUint16);
const fidl_type kUint32Table(FidlCodedPrimitive::kUint32);
const fidl_type kUint64Table(FidlCodedPrimitive::kUint64);
const fidl_type kFloat32Table(FidlCodedPrimitive::kFloat32);
const fidl_type kFloat64Table(FidlCodedPrimitive::kFloat64);

} // namespace internal

} // namespace fidl
