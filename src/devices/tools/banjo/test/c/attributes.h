// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.attributes banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct none_struct none_struct_t;
typedef struct packed_struct packed_struct_t;
typedef union none_union none_union_t;
typedef union packed_union packed_union_t;

// Declarations
struct none_struct {
    int32_t foo;
    int32_t bar;
    int32_t baz;
};

struct packed_struct {
    int32_t foo;
    int32_t bar;
    int32_t baz;
} __PACKED;

union none_union {
    int32_t foo;
    int32_t bar;
    int32_t baz;
};

union packed_union {
    int32_t foo;
    int32_t bar;
    int32_t baz;
} __PACKED;


__END_CDECLS
