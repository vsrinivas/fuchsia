// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.order6 banjo file

#pragma once

#include <banjo/examples/order7/c/banjo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef int8_t random_t;
#define RANDOM_ONE INT8_C(2)
#define RANDOM_TWO INT8_C(7)
#define RANDOM_THREE INT8_C(3)
typedef union foo foo_t;
typedef struct bar bar_t;

// Declarations
union foo {
    uint64_t code;
    one_t one;
};

struct bar {
    two_t two;
    int32_t value;
};


// Helpers


__END_CDECLS
