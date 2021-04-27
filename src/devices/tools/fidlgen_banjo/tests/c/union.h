// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.union banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef union with_ordinal with_ordinal_t;
typedef union formerly_without_ordinal formerly_without_ordinal_t;

// Declarations
union with_ordinal {
    const char* one;
    int8_t two;
    uint32_t three[3];
};

union formerly_without_ordinal {
    const char* one;
    int8_t two;
    uint32_t three[3];
};


// Helpers


__END_CDECLS
