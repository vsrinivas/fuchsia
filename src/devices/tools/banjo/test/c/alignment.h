// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.alignment banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct packing_0 packing_0_t;
typedef struct packing_1 packing_1_t;
typedef struct packing_2 packing_2_t;
typedef struct packing_3 packing_3_t;

// Declarations
struct packing_0 {
    int16_t i16_0;
    int32_t i32_0;
    int16_t i16_1;
} __PACKED;

struct packing_1 {
    int16_t i16_0;
    int8_t i8_0;
    int16_t i16_1;
    int8_t i8_1;
} __PACKED;

struct packing_2 {
    int16_t i16_0;
    int8_t i8_0;
    int8_t i8_1;
    int16_t i16_1;
} __PACKED;

struct packing_3 {
    int32_t i32_0;
    int64_t i64_0;
    int16_t i16_0;
    int32_t i32_1;
    int16_t i16_1;
} __PACKED;


__END_CDECLS
