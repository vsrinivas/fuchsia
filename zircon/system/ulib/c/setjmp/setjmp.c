// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include "setjmp_impl.h"

static_assert(sizeof(__jmp_buf) == sizeof(uint64_t) * JB_COUNT,
              "fix __jmp_buf definition");
