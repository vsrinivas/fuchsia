// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <stdio.h>

// TODO(johngro) : replace this with a system which...
//
// 1) Uses low overhead loging service infrastructure instead of printf.
// 2) Uses C/C++ functions (either template parameter packs, or c-style
//    var-args) instead of preprocessor macros.

#define VERBOSE_LOGGING 0
#define DEBUG_LOGGING (VERBOSE_LOGGING || 0)

#define LOG_EX(obj, ...) do { \
    (obj).PrintDebugPrefix(); \
    printf(__VA_ARGS__);      \
} while (false)

#define LOG(...) LOG_EX(*this, __VA_ARGS__)

#define DEBUG_LOG_EX(obj, ...) do {     \
    if (DEBUG_LOGGING) {                \
        (obj).PrintDebugPrefix();       \
        printf(__VA_ARGS__);            \
    }                                   \
} while (false)

#define DEBUG_LOG(...) DEBUG_LOG_EX(*this, __VA_ARGS__)

#define VERBOSE_LOG_EX(obj, ...) do {   \
    if (VERBOSE_LOGGING) {              \
        (obj).PrintDebugPrefix();       \
        printf(__VA_ARGS__);            \
    }                                   \
} while (false)

#define VERBOSE_LOG(...) VERBOSE_LOG_EX(*this, __VA_ARGS__)
