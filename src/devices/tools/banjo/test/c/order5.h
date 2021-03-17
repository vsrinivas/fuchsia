// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.order5 banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef uint16_t great_type_t;
typedef struct blabla_something_response blabla_something_response_t;
typedef struct blabla_something_request blabla_something_request_t;

// Declarations
struct blabla_something_response {
    zx_status_t status;
    uint64_t value;
    uint16_t g_two;
};

struct blabla_something_request {
    uint32_t opcode;
    uint16_t g_one;
};


// Helpers


__END_CDECLS
