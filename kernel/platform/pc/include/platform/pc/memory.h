// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef struct e820entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __PACKED e820entry_t;
