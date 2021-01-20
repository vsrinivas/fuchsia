// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.references banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct mutable_field mutable_field_t;
typedef struct some_type some_type_t;
typedef struct vector_field_in_struct vector_field_in_struct_t;

// Declarations
struct mutable_field {
    char* some_string;
    char* some_other_string;
    const char* some_default_string;
};

struct some_type {
    uint32_t value;
};

struct vector_field_in_struct {
    const some_type_t** the_vector_list;
    size_t the_vector_count;
    const some_type_t** the_other_vector_list;
    size_t the_other_vector_count;
    const some_type_t* the_default_vector_list;
    size_t the_default_vector_count;
};


__END_CDECLS
