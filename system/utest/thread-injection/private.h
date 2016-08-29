// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdatomic.h>

struct helper_data {
    atomic_int* futex_addr;
    mx_handle_t bootstrap;
};

#define MAGIC 0xb00bee
