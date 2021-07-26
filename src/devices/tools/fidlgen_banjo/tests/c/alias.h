// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.alias banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations

typedef uint32_t first_primitive_t;


typedef struct some_struct some_struct_t;
typedef some_struct_t second_struct_t;

// Declarations
struct some_struct {
    uint16_t one;
    uint32_t two;
    uint32_t primitive;
    const uint8_t* vector_alias_list;
    size_t vector_alias_count;
    uint8_t array_alias[32];
    uint8_t nested_alias[32][32];
};


// Helpers


__END_CDECLS
