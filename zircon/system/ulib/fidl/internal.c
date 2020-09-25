// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

// Coding tables for primitives are predefined and interned here.
// This file must be a .c to guarantee that these types are stored directly in
// .rodata, rather than requiring global ctors to have been run (fxbug.dev/39978).
const struct FidlCodedPrimitive fidl_internal_kBoolTable = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Bool};
const struct FidlCodedPrimitive fidl_internal_kInt8Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Int8};
const struct FidlCodedPrimitive fidl_internal_kInt16Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Int16};
const struct FidlCodedPrimitive fidl_internal_kInt32Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Int32};
const struct FidlCodedPrimitive fidl_internal_kInt64Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Int64};
const struct FidlCodedPrimitive fidl_internal_kUint8Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Uint8};
const struct FidlCodedPrimitive fidl_internal_kUint16Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Uint16};
const struct FidlCodedPrimitive fidl_internal_kUint32Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Uint32};
const struct FidlCodedPrimitive fidl_internal_kUint64Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Uint64};
const struct FidlCodedPrimitive fidl_internal_kFloat32Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Float32};
const struct FidlCodedPrimitive fidl_internal_kFloat64Table = {
    .tag = kFidlTypePrimitive, .type = kFidlCodedPrimitiveSubtype_Float64};
