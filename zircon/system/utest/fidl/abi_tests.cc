// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>

#include <type_traits>

namespace fidl {
namespace {

// All the data in coding tables should be pure data.
static_assert(std::is_standard_layout<FidlTypeTag>::value, "");
static_assert(std::is_standard_layout<FidlStructField>::value, "");
static_assert(std::is_standard_layout<FidlTableField>::value, "");
static_assert(std::is_standard_layout<FidlCodedStruct>::value, "");
static_assert(std::is_standard_layout<FidlCodedStructPointer>::value, "");
static_assert(std::is_standard_layout<FidlCodedXUnion>::value, "");
static_assert(std::is_standard_layout<FidlCodedArray>::value, "");
static_assert(std::is_standard_layout<FidlCodedArrayNew>::value, "");
static_assert(std::is_standard_layout<FidlCodedVector>::value, "");
static_assert(std::is_standard_layout<FidlCodedString>::value, "");
static_assert(std::is_standard_layout<FidlCodedHandle>::value, "");

static_assert(offsetof(FidlCodedStruct, tag) == 0, "");
static_assert(offsetof(FidlCodedStructPointer, tag) == 0, "");
static_assert(offsetof(FidlCodedXUnion, tag) == 0, "");
static_assert(offsetof(FidlCodedArray, tag) == 0, "");
static_assert(offsetof(FidlCodedArrayNew, tag) == 0, "");
static_assert(offsetof(FidlCodedVector, tag) == 0, "");
static_assert(offsetof(FidlCodedString, tag) == 0, "");
static_assert(offsetof(FidlCodedHandle, tag) == 0, "");

// Take caution when increasing the size numbers below. While they
// can be changed as needed when the structure evolves, these growing
// has a large impact on binary size and memory footprint.

static_assert(sizeof(struct FidlCodedPrimitive) == 2, "");
static_assert(sizeof(struct FidlCodedEnum) == 24, "");
static_assert(sizeof(struct FidlCodedBits) == 24, "");
static_assert(sizeof(struct FidlCodedStruct) == 24, "");
static_assert(sizeof(struct FidlCodedStructPointer) == 16, "");
static_assert(sizeof(struct FidlCodedXUnion) == 24, "");
static_assert(sizeof(struct FidlCodedArray) == 16, "");
static_assert(sizeof(struct FidlCodedArrayNew) == 24, "");
static_assert(sizeof(struct FidlCodedVector) == 24, "");
static_assert(sizeof(struct FidlCodedString) == 8, "");
static_assert(sizeof(struct FidlCodedHandle) == 12, "");

static_assert(sizeof(struct FidlStructField) == 16, "");
static_assert(sizeof(struct FidlTableField) == 16, "");
static_assert(sizeof(struct FidlXUnionField) == 16, "");

}  // namespace
}  // namespace fidl
