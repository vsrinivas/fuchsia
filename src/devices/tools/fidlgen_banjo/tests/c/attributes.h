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
typedef union packed_union packed_union_t;
typedef struct packed_struct packed_struct_t;
typedef union none_union none_union_t;
typedef struct none_struct none_struct_t;

// Declarations
union packed_union {
    int32_t foo;
    int32_t bar;
    int32_t baz;
} __attribute__ ((packed));

struct packed_struct {
    int32_t foo;
    int32_t bar;
    int32_t baz;
} __attribute__ ((packed));

union none_union {
    int32_t foo;
    int32_t bar;
    int32_t baz;
};

struct none_struct {
    int32_t foo;
    int32_t bar;
    int32_t baz;
};


// Helpers


__END_CDECLS
