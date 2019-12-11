// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

// Coding tables for primitives are predefined and interned here.
// This file must be a .c to guarantee that these types are stored directly in
// .rodata, rather than requiring global ctors to have been run (fxb/39978).
const fidl_type_t fidl_internal_kBoolTable = {.type_tag = kFidlTypePrimitive,
                                              .coded_primitive = kFidlCodedPrimitive_Bool};
const fidl_type_t fidl_internal_kInt8Table = {.type_tag = kFidlTypePrimitive,
                                              .coded_primitive = kFidlCodedPrimitive_Int8};
const fidl_type_t fidl_internal_kInt16Table = {.type_tag = kFidlTypePrimitive,
                                               .coded_primitive = kFidlCodedPrimitive_Int16};
const fidl_type_t fidl_internal_kInt32Table = {.type_tag = kFidlTypePrimitive,
                                               .coded_primitive = kFidlCodedPrimitive_Int32};
const fidl_type_t fidl_internal_kInt64Table = {.type_tag = kFidlTypePrimitive,
                                               .coded_primitive = kFidlCodedPrimitive_Int64};
const fidl_type_t fidl_internal_kUint8Table = {.type_tag = kFidlTypePrimitive,
                                               .coded_primitive = kFidlCodedPrimitive_Uint8};
const fidl_type_t fidl_internal_kUint16Table = {.type_tag = kFidlTypePrimitive,
                                                .coded_primitive = kFidlCodedPrimitive_Uint16};
const fidl_type_t fidl_internal_kUint32Table = {.type_tag = kFidlTypePrimitive,
                                                .coded_primitive = kFidlCodedPrimitive_Uint32};
const fidl_type_t fidl_internal_kUint64Table = {.type_tag = kFidlTypePrimitive,
                                                .coded_primitive = kFidlCodedPrimitive_Uint64};
const fidl_type_t fidl_internal_kFloat32Table = {.type_tag = kFidlTypePrimitive,
                                                 .coded_primitive = kFidlCodedPrimitive_Float32};
const fidl_type_t fidl_internal_kFloat64Table = {.type_tag = kFidlTypePrimitive,
                                                 .coded_primitive = kFidlCodedPrimitive_Float64};
