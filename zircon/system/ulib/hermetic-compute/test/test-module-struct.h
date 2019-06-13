// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <type_traits>

struct OneWord {
    uintptr_t x;
};
static_assert(sizeof(OneWord) == sizeof(uintptr_t));

struct MultiWord {
    uintptr_t x, y, z;
};
static_assert(sizeof(MultiWord) > sizeof(uintptr_t));
static_assert(sizeof(MultiWord) % sizeof(uintptr_t) == 0);

struct Tiny {
    uint8_t x, y;
};
static_assert(sizeof(Tiny) < sizeof(uintptr_t));

struct Odd {
    uint8_t x[13];
    int Total() const {
        int n = 0;
        for (auto elt : x) {
            n += elt;
        }
        return n;
    }
};
static_assert(sizeof(Odd) % sizeof(uintptr_t) != 0);

inline Odd MakeOdd() {
    uint8_t n = 7;
    Odd result;
    for (auto& elt : result.x) {
        elt = ++n;
    }
    return result;
}
